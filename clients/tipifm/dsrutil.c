#include "dsrutil.h"
#include "mds_dsrlnk.h"

#include <conio.h>
#include "strutil.h"
#include "tifloat.h"
#include <string.h>

struct DeviceServiceRoutine dsrList[40];

unsigned char status(struct DeviceServiceRoutine* dsr, const char* pathname, int vdpbuffer) {
  struct PAB pab;
  initPab(&pab);
  pab.pName = (char*)pathname;
  pab.VDPBuffer = vdpbuffer;

  unsigned char ferr = dsr_status(dsr, &pab);
  if (ferr != DSR_ERR_NONE) {
    return 0;
  }

  return pab.Status;
}

unsigned char loadDir(struct DeviceServiceRoutine* dsr, const char* pathname, vol_entry_cb vol_cb, dir_entry_cb dir_cb) {
  struct PAB pab;
  
  struct VolInfo volInfo;
  struct DirEntry dirEntry;

  unsigned char ferr = dsr_open(dsr, &pab, pathname, FBUF, DSR_TYPE_INPUT | DSR_TYPE_INTERNAL | DSR_TYPE_SEQUENTIAL, 38);
  if (ferr) {
    return ferr;
  }

  int recNo = 0;
  ferr = DSR_ERR_NONE;
  while(ferr == DSR_ERR_NONE) {
    unsigned char cbuf[38];
    ferr = dsr_read(dsr, &pab, recNo);
    if (ferr == DSR_ERR_NONE) {
      // Now FBUF has the data... 
      vdpmemread(FBUF, cbuf, pab.CharCount);
      // process Record
      if (recNo == 0) {
        if (cbuf[0] == 0) {
          cprintf("no device\n");
          return 0;
        }
        basicToCstr(cbuf, volInfo.volname);
        vol_cb(&volInfo);
      } else {
        int namlen = basicToCstr(cbuf, dirEntry.name);
        if (namlen == 0) {
          break;
        }
        int a = ti_floatToInt(cbuf+1+namlen);
        int j = ti_floatToInt(cbuf+10+namlen);
        int k = ti_floatToInt(cbuf+19+namlen);
        dirEntry.type = a;
        dirEntry.sectors = j;
        dirEntry.reclen = k;
        if (dirEntry.name[0] != 0) {
          dir_cb(&dirEntry);
        }
      }
      recNo++;
    } else {
      break;
    }
  }

  ferr = dsr_close(dsr, &pab);
  if (ferr) {
    return ferr;
  }
}


//---- the following are meant to be easy, not fast ----

void initPab(struct PAB* pab) {
  pab->OpCode = DSR_OPEN;
  pab->Status = DSR_TYPE_DISPLAY | DSR_TYPE_VARIABLE | DSR_TYPE_SEQUENTIAL | DSR_TYPE_INPUT;
  pab->RecordLength = 80;
  pab->RecordNumber = 0;
  pab->ScreenOffset = 0;
  pab->NameLength = 0;
  pab->CharCount = 0;
}

unsigned char dsr_open(struct DeviceServiceRoutine* dsr, struct PAB* pab, const char* fname, int vdpbuffer, unsigned char flags, int reclen) {
  initPab(pab);
  pab->OpCode = DSR_OPEN;
  if (flags != 0) {
    pab->Status = flags;
  }
  if (reclen != 0) {
    pab->RecordLength = reclen;
  }
  pab->pName = (char*)fname;
  pab->VDPBuffer = vdpbuffer;

  return mds_dsrlnk(dsr->crubase, pab, VPAB);
}

unsigned char dsr_close(struct DeviceServiceRoutine* dsr, struct PAB* pab) {
  pab->OpCode = DSR_CLOSE;

  return mds_dsrlnk(dsr->crubase, pab, VPAB);
}

// the data read is in FBUF, the length read in pab->CharCount
// typically passing 0 in for record number will let the controller
// auto-increment it. 
unsigned char dsr_read(struct DeviceServiceRoutine* dsr, struct PAB* pab, int recordNumber) {
  pab->OpCode = DSR_READ;
  pab->RecordNumber = recordNumber;
  pab->CharCount = 0;

  unsigned char result = mds_dsrlnk(dsr->crubase, pab, VPAB);
  vdpmemread(VPAB + 5, (&pab->CharCount), 1);
  return result;
}

unsigned char dsr_status(struct DeviceServiceRoutine* dsr, struct PAB* pab) {
  pab->OpCode = DSR_STATUS;

  unsigned char result = mds_dsrlnk(dsr->crubase, pab, VPAB);
  return result;
}

void loadDriveDSRs() {
  struct DeviceServiceRoutine* listHead = dsrList;

  int cruscan = 0x1000;
  while(cruscan < 0x2000) {
    enableROM(cruscan);
    struct DeviceRomHeader* dsrrom = (struct DeviceRomHeader*) 0x4000;
    if (dsrrom->flag == 0xAA) {

      struct NameLink* dsrlinks = dsrrom->dsrlnk;

      while(dsrlinks != 0) {
        
        if (isDrive(dsrlinks->name)) {
          basicToCstr(dsrlinks->name, listHead->name);
          listHead->crubase = cruscan;
          listHead->addr = dsrlinks->routine;
          listHead += 1;
        }
        
        dsrlinks = dsrlinks->next;
      }
    }

    disableROM(cruscan);
    cruscan += 0x0100;
  }
}

int isDrive(char* basicstr) {
  if (basicstr[0] == 4) {
    if (0 == basic_strcmp(basicstr, "TIPI")) {
      return 1;
    } else if (basicstr[1] >= 'A' && basicstr[1] <= 'Z' && basicstr[4] >= '0' && basicstr[4] <= '9') {
      return 1;
    }
  }
  return 0;
}

void enableROM(int crubase) {
  __asm__("mov %0,r12\n\tsbo 0" : : "r"(crubase) : "r12");
}

void disableROM(int crubase) {
  __asm__("mov %0,r12\n\tsbz 0" : : "r"(crubase) : "r12");
}

struct DeviceServiceRoutine* findDsr(char* devicename, int crubase) {
  int i = 0;

  while(dsrList[i].name[0] != 0) {
    if (crubase != 0) {
      if (dsrList[i].crubase == crubase && 0 == strcmp(dsrList[i].name, devicename)) {
        return &dsrList[i];
      }
    } else {
      if (0 == strcmp(dsrList[i].name, devicename)) {
        return &dsrList[i];
      }
    }
    i++;
  }

  return 0;
}