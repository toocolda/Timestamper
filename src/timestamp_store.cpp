#include "features/timestamp.h"

#include <EEPROM.h>

// EEPROM layout:
// [0] count (0..99)
// [1] oldest physical index (0..98)
// [2..] records
static const int kAddrCount = 0;
static const int kAddrOldest = 1;
static const int kAddrRecords = 2;

struct PackedStamp {
  uint8_t yOff;   // year offset from 2000
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
};

static const uint8_t kRecordSize = sizeof(PackedStamp);

static uint8_t g_count = 0;
static uint8_t g_oldest = 0;
static bool g_loaded = false;

static void loadMeta() {
  if (g_loaded) return;

  g_count = EEPROM.read(kAddrCount);
  g_oldest = EEPROM.read(kAddrOldest);

  if (g_count > TIMESTAMP_STORE_MAX) g_count = 0;
  if (g_oldest >= TIMESTAMP_STORE_MAX) g_oldest = 0;
  // Guard the ring-buffer invariant against partially corrupted EEPROM meta:
  // the oldest index must point inside the populated range.
  if (g_count > 0 && g_oldest >= g_count) g_oldest = 0;

  g_loaded = true;
}

static void saveMeta() {
  EEPROM.update(kAddrCount, g_count);
  EEPROM.update(kAddrOldest, g_oldest);
}

static uint8_t physicalFromChrono(uint8_t chronoIndex) {
  return (uint8_t)((g_oldest + chronoIndex) % TIMESTAMP_STORE_MAX);
}

static int addrFromPhysical(uint8_t physicalIndex) {
  return kAddrRecords + (int)physicalIndex * (int)kRecordSize;
}

static PackedStamp packStamp(const TimeEdit_t* t) {
  PackedStamp p;
  uint16_t year = t->year;
  if (year < 2000) year = 2000;
  if (year > 2255) year = 2255;
  p.yOff = (uint8_t)(year - 2000);
  p.month = t->month;
  p.day = t->day;
  p.hour = t->hour;
  p.minute = t->minute;
  p.second = t->second;
  return p;
}

static TimeEdit_t unpackStamp(const PackedStamp* p) {
  TimeEdit_t t;
  t.year = (uint16_t)(2000 + p->yOff);
  t.month = p->month;
  t.day = p->day;
  t.hour = p->hour;
  t.minute = p->minute;
  t.second = p->second;
  return t;
}

static void writePhysical(uint8_t physicalIndex, const PackedStamp* p) {
  int a = addrFromPhysical(physicalIndex);
  EEPROM.update(a + 0, p->yOff);
  EEPROM.update(a + 1, p->month);
  EEPROM.update(a + 2, p->day);
  EEPROM.update(a + 3, p->hour);
  EEPROM.update(a + 4, p->minute);
  EEPROM.update(a + 5, p->second);
}

static PackedStamp readPhysical(uint8_t physicalIndex) {
  int a = addrFromPhysical(physicalIndex);
  PackedStamp p;
  p.yOff = EEPROM.read(a + 0);
  p.month = EEPROM.read(a + 1);
  p.day = EEPROM.read(a + 2);
  p.hour = EEPROM.read(a + 3);
  p.minute = EEPROM.read(a + 4);
  p.second = EEPROM.read(a + 5);
  return p;
}

// newestIndex: 0=newest, count-1=oldest
static bool chronoFromNewest(uint8_t newestIndex, uint8_t* chronoOut) {
  if (newestIndex >= g_count || chronoOut == nullptr) return false;
  *chronoOut = (uint8_t)((g_count - 1) - newestIndex);
  return true;
}

void timestampStoreAdd(const TimeEdit_t* stamp) {
  if (stamp == nullptr) return;
  loadMeta();

  PackedStamp p = packStamp(stamp);

  if (g_count < TIMESTAMP_STORE_MAX) {
    uint8_t insertPhys = physicalFromChrono(g_count);
    writePhysical(insertPhys, &p);
    g_count++;
    saveMeta();
    return;
  }

  // Full: overwrite oldest and move oldest pointer forward.
  writePhysical(g_oldest, &p);
  g_oldest = (uint8_t)((g_oldest + 1) % TIMESTAMP_STORE_MAX);
  saveMeta();
}

uint8_t timestampStoreCount() {
  loadMeta();
  return g_count;
}

bool timestampStoreGetByNewest(uint8_t newestIndex, TimeEdit_t* outStamp) {
  if (outStamp == nullptr) return false;
  loadMeta();

  uint8_t chronoIndex = 0;
  if (!chronoFromNewest(newestIndex, &chronoIndex)) return false;

  uint8_t phys = physicalFromChrono(chronoIndex);
  PackedStamp p = readPhysical(phys);
  *outStamp = unpackStamp(&p);
  return true;
}

bool timestampStoreDeleteByNewest(uint8_t newestIndex) {
  loadMeta();

  uint8_t removeChrono = 0;
  if (!chronoFromNewest(newestIndex, &removeChrono)) return false;

  if (g_count == 0) return false;

  // Fast path: deleting the oldest only advances the ring head.
  if (removeChrono == 0) {
    g_oldest = (uint8_t)((g_oldest + 1U) % TIMESTAMP_STORE_MAX);
    g_count--;
    saveMeta();
    return true;
  }

  // Fast path: deleting the newest only shrinks the count.
  if (removeChrono + 1U == g_count) {
    g_count--;
    saveMeta();
    return true;
  }

  uint8_t newerCount = (uint8_t)((g_count - 1U) - removeChrono);
  uint8_t olderCount = removeChrono;

  if (olderCount < newerCount) {
    // Shift the older side one slot toward newer entries, then advance head.
    for (uint8_t chrono = removeChrono; chrono > 0U; chrono--) {
      uint8_t srcPhys = physicalFromChrono((uint8_t)(chrono - 1U));
      uint8_t dstPhys = physicalFromChrono(chrono);
      PackedStamp p = readPhysical(srcPhys);
      writePhysical(dstPhys, &p);
    }
    g_oldest = (uint8_t)((g_oldest + 1U) % TIMESTAMP_STORE_MAX);
  } else {
    // Shift the newer side one slot toward older entries.
    for (uint8_t chrono = removeChrono; chrono + 1U < g_count; chrono++) {
      uint8_t srcPhys = physicalFromChrono((uint8_t)(chrono + 1U));
      uint8_t dstPhys = physicalFromChrono(chrono);
      PackedStamp p = readPhysical(srcPhys);
      writePhysical(dstPhys, &p);
    }
  }

  g_count--;
  saveMeta();
  return true;
}

void timestampStoreClearAll() {
  loadMeta();
  g_count = 0;
  g_oldest = 0;
  saveMeta();
}
