
#include "terminal.h"
#include <conio.h>
#include <string.h>
#include "ti_socket.h"
#include "terminal.h"

// Cheating, and reaching into conio_cputc.c
void inc_row();

#define COLOR_DEFAULT 7

int stage;
unsigned char bytestr[128];
int bs_idx;

int termWidth;

unsigned char cursor_store_x;
unsigned char cursor_store_y;

#define STAGE_OPEN 0
#define STAGE_ESC 1
#define STAGE_CSI 2

void resetState() {
  stage = STAGE_OPEN;
  bs_idx = 0;
}

void initTerminal() {
  cursor_store_x = 0;
  cursor_store_y = 0;

  resetState();
}

int getParamA(int def) {
  int i = 0;
  while(i<bs_idx && bytestr[i] != ';') {
    i++;
  }
  if (i == 0) {
    return def;
  }
  return atoi(bytestr);
}

int getParamB(int def) {
  int i = 0;
  while(i<bs_idx && bytestr[i] != ';') {
    i++;
  }
  // we should be pointing to beginning of param B in buffer.
  if (i == 0 || i == bs_idx) {
    return def;
  }
  unsigned char* paramb = bytestr + i + 1;
  return atoi(paramb);
}

void cursorUp(int lines) {
  int y = wherey() - lines;
  if (y < 0) {
    y = 0;
  }
  gotoy(y);
}

void cursorDown(int lines) {
  int y = wherey() + lines;
  unsigned char scx;
  unsigned char scy;
  screensize(&scx, &scy);
  scy--;
  if (y > scy) {
    y = scy;
  }
  gotoy(y);
}

void cursorRight(int cols) {
  int x = wherex() + cols;
  unsigned char scx;
  unsigned char scy;
  screensize(&scx, &scy);
  scx--;
  if (x > scx) {
    x = scx;
  }
  gotox(x);
}

void cursorLeft(int cols) {
  int x = wherex() + cols;
  if (x < 0) {
    x = 0;
  }
  gotox(x);
}

void cursorGoto(int x, int y) {
  unsigned char scx;
  unsigned char scy;
  screensize(&scx, &scy);
  if (x > scx) {
    x = scx;
  } else if (x < 1) {
    x = 1;
  }
  if (y > scy) {
    y = scy;
  } else if (y < 1) {
    y = 1;
  }
  // zero based screen
  gotox(x - 1);
  gotoy(y - 1);
}

void eraseDisplay(int opt) {
  unsigned char scx;
  unsigned char scy;
  screensize(&scx, &scy);
  int oldx = wherex();
  int oldy = wherey();
  int cursorAddr = gImage + (oldx + (oldy + scy));

  switch (opt) {
    case 0: // clear from cursor to end of screen, remain at location
      cclear(scx - oldx + scx*(scy - oldy - 1) - 1); // clear except last character - avoid scrolling
      gotoxy(oldx, oldy);
      break;
    case 1: // clear from cursor to beginning of screen, remain at location
      cclearxy(0,0, scx*oldy + oldx); // should end up at oldx,oldy
      break;
    case 3: // TODO: if we add scroll back buffer, 3 should clear that too.
    case 2: // clear full screen, return to top
      clrscr();
      gotoxy(0,0);
      break;
  }
}

void eraseLine(int opt) {
  unsigned char scx;
  unsigned char scy;
  screensize(&scx, &scy);
  int oldx = wherex();
  int oldy = wherey();
  switch (opt) {
    case 0: // to end of line
      cclear(scx - oldx);
      break;
    case 1: // to beginning of line
      gotox(0);
      cclear(oldx);
      break;
    case 2: // entire line
      gotox(0);
      cclear(scx);
      break;
  }
  gotoxy(oldx, oldy);
}

void scrollUp(int lc) {
  gotoxy(0,23);
  for (int i = 0; i < lc; i++) {
    inc_row();
  }
}

void sendTermCoord() {
  unsigned int x = wherex() + 1;
  unsigned int y = wherey() + 1;
  unsigned char buf[30];
  buf[0] = 27;
  buf[1] = '[';
  unsigned char* cursor = buf + 2;
  strcpy(cursor, int2str(y));
  cursor = buf + strlen(buf);
  *cursor = ';';
  cursor++;
  strcpy(cursor, int2str(x));
  cursor = buf + strlen(buf);
  *cursor = 'R';
  cursor++;
  *cursor = 0;
  send_chars(buf, strlen(buf));
}

void sendTermType() {
  unsigned char buf[8];
  buf[0] = 27;
  strcpy(buf + 1, "[?1;0c");
  send_chars(buf, 7);
}

unsigned char isBold = 0;

unsigned char colors[16] = {
  COLOR_BLACK,
  COLOR_MEDRED,
  COLOR_MEDGREEN,
  COLOR_DKYELLOW,
  COLOR_DKBLUE,
  COLOR_MAGENTA,
  COLOR_CYAN,
  COLOR_GRAY,
  // now the BOLD variety
  COLOR_GRAY,
  COLOR_LTRED,
  COLOR_LTGREEN,
  COLOR_LTYELLOW,
  COLOR_LTBLUE,
  COLOR_DKRED, // bold magenta?
  COLOR_CYAN, // ??? 
  COLOR_WHITE
};

unsigned char foreground = COLOR_DEFAULT;
unsigned char background = 0;

void setColors() {
  bgcolor(colors[background]);
  textcolor(colors[foreground | isBold]);
}

void doSGRCommand() {
  // only support color in 80 column mode.
  if (termWidth != 80) {
    return;
  }

  // each param ( there can be n ) is processed to set attributes
  // on all subsequent text. We'll only support foreground and
  // background color.
  // bs_idx
  // bytestr
  if (bs_idx == 0) {
    // set defaults and return
    isBold = 0;
    foreground = COLOR_DEFAULT;
    background = 0;
    setColors();
    return;
  }
  int i = 0;
  unsigned char* params = bytestr;
  while(i < bs_idx) {
    int sgr = atoi(params + i);
    switch(sgr) {
      case 0: // clear attrs
        isBold = 0;
        foreground = COLOR_DEFAULT;
        background = 0;
        break;
      case 1: // bold
        isBold = 8;
        break;
      case 30: // standard foregrounds
      case 31:
      case 32:
      case 33:
      case 34:
      case 35:
      case 36:
      case 37:
        foreground = sgr - 30;
        break;
      case 40: // standard backgrounds
      case 41:
      case 42:
      case 43:
      case 44:
      case 45:
      case 46:
      case 47:
        background = sgr - 40;
        break;
      case 90: // high intensity fore
      case 91:
      case 92:
      case 93:
      case 94:
      case 95:
      case 96:
      case 97:
        foreground = (sgr - 90) | 8;
        break;
      case 100: // high intensity background
      case 101:
      case 102:
      case 103:
      case 104:
      case 105:
      case 106:
      case 107:
        background = (sgr - 100) | 8;
        break;
    }
    setColors();
    while(i < bs_idx && params[i] >= '0' && params[i] <= '9') {
      i++;
    }
    if (i < bs_idx && params[i] == ';') {
      i++;
    }
  }
  bytestr[0] = 0;
  bs_idx = 0;
}

void doCsiCommand(unsigned char c) {
  // Note, ANSI cursor locations (1,1) upper left corner.
  switch (c) {
    case 'A': // cursor UP, 1 param, default 1
      cursorUp(getParamA(1));
      break;
    case 'B': // cursor DOWN, 1 param, default 1
      cursorDown(getParamA(1));
      break;
    case 'C': // cursor RIGHT, 1 param, default 1
      cursorRight(getParamA(1));
      break;
    case 'D': // cursor LEFT, 1 param, default 1
      cursorLeft(getParamA(1));
      break;
    case 'E': // cursor next line, 1 param, default 1
      cursorDown(getParamA(1));
      gotox(0);
      break;
    case 'F': // cursor prev line, 1 param, default 1
      cursorUp(getParamA(1));
      gotox(0);
      break;
    case 'G': // set cursor column, 1 param, default 1
      cursorGoto(getParamA(1),wherey() + 1);
      break;
    case 'H': // set position, 2 param, defaults 1, 1
      cursorGoto(getParamB(1), getParamA(1));
      break;
    case 'f': // synonym
      cursorGoto(getParamB(1), getParamA(1));
      break;
    case 'J': // erase in display, 1 param, default 0
      eraseDisplay(getParamA(0));
      break;
    case 'K': // erase in line, 1 param, default 0
      eraseLine(getParamA(0));
      break;
    case 'S': // scroll up lines, 1 param, default 1
      scrollUp(getParamA(1));
      break;
    case 'T': // scroll down lines, 1 param, default 1
      charout('^'); // just note that I didn't handle this.
      break;
    case 'm': // color (SGR), n params
      doSGRCommand();
      break;
    case 'n': // some introspection
      {
        if (getParamA(0) == 6) {
          sendTermCoord();
        }
      }
    case 's': // store cursor, no params
      cursor_store_x = wherex();
      cursor_store_y = wherey();
      break;
    case 'u': // restore cursor, no params.
      gotoxy(cursor_store_x, cursor_store_y);
      break;
    case 'c': // identify term type.
      sendTermType();
      break;
  }
}

#define ESC_OPEN 1
#define ESC_CLOSED 0
#define ESC_FIVE 5
#define ESC_SIX 6
#define ESC_Y 'Y'
#define ESC_X 'X'

unsigned char esc_state = ESC_OPEN;

int doEscCommand(unsigned char c) {
  if (esc_state == ESC_OPEN) {
    switch (c) {
      case '7': // save cursor
        cursor_store_x = wherex();
        cursor_store_y = wherey();
        break;
      case '8': // restore cursor
        gotoxy(cursor_store_x, cursor_store_y);
        break;
      case '5': // possible status
        esc_state = ESC_FIVE;
        break;
      case '6': // possible status
        esc_state = ESC_SIX;
        break;
      case 'Z': // vt52 ident
        {
          unsigned char buf[4];
          buf[0] = 27;
          buf[1] = '/';
          buf[2] = 'Z';
          buf[3] = 0;
          send_chars(buf, 3);
        }
        break;
      case 'Y': // vt52 goto
        esc_state = ESC_Y;
        break;
      default:
        return 1; // done.
    }
  } else {
    if (esc_state == ESC_Y) {
      gotoy(c - 32);
      esc_state == ESC_X;
      return 0;
    } else if (esc_state == ESC_X) {
      gotox(c - 32);
      return 1;
    } else if (c == 'n') {
      if (esc_state == ESC_FIVE) {
        // send term ok   ^[0n
        unsigned char buf[3];
        buf[0] = 27;
        buf[1] = '0';
        buf[2] = 'n';
        send_chars(buf, 3);
        return 1;
      } else if (esc_state == ESC_SIX) {
        sendTermCoord();
        return 1;
      }
    } 
  }
  return 0;
}

void charout(unsigned char ch) {
  switch (ch) {
    case '\r': // carriage return
      conio_x=0;
      break;
    case '\n': // line feed
      conio_x=0;
      inc_row();
      break;
    case '\b': // backspace
      --conio_x;
      if (conio_x < 0) {
        conio_x = nTextEnd-nTextRow+1;
        if (conio_y > 0) --conio_y;
      }
      break;
    default: // it is important to handle control codes before choosing to wrap to next line.
      if (ch >= ' ') {
        if (conio_x >= nTextEnd-nTextRow) {
          conio_x=0;
          inc_row();
        }
        vdpchar(conio_getvram(), ch);
        ++conio_x;
      }
    break;
  }
}

void terminalDisplay(unsigned char c) {
  if (stage == STAGE_OPEN) {
    if (c == 27) {
      stage = STAGE_ESC;
      esc_state = ESC_OPEN;
    } else {
      charout(c);
    }
  } else if (stage == STAGE_ESC) {
    if (c == '[') {
      // command begins
      stage = STAGE_CSI;
      bs_idx = 0;
      bytestr[0] = 0;
    } else {
      // pass on to legacy state machine.
      int escDone = doEscCommand(c);
      if (escDone) {
        stage = STAGE_OPEN;
        esc_state = ESC_CLOSED;
      }
    }
  } else if (stage == STAGE_CSI) { 
    if (c >= 0x40 && c <= 0x7E) {
      // command complete
      doCsiCommand(c);
      stage = STAGE_OPEN;
    } else if (c >= 0x30 && c <= 0x3F) {
      // capture params. If we still have room in our buffer
      if (bs_idx < 128) {
        bytestr[bs_idx] = c;
        bs_idx++;
        bytestr[bs_idx] = 0;
      }
    } else {
      // this is basically an error state... ignoring the command.
      stage = STAGE_OPEN;
    }
  }
}

void terminalKey(unsigned char* buf, int* len) {
  // Length must be set to 1 before calling

  // translate output keys into correct terminal keyboard commands
  // TI control keys ctrl-a = 129 ---> ctrl-z = 154
  if (buf[0] >= 129 && buf[0] <= 154) {
    buf[0] = buf[0] - 128;
    return;
  }
  
  switch (buf[0]) {
    case 1: // tab
      buf[0] = 9;
      break;
    case 8: // left-arrrow
      /*      
      buf[0] = 27; // esc
      buf[1] = 'D';
      *len = 2;
      */
      buf[0] = '\b';
      break;
    case 9: // right-arrow
      buf[0] = 27;
      buf[1] = 'C';
      *len = 2;
      break;
    case 10: // down-arrow
      buf[0] = 27;
      buf[1] = 'B';
      *len = 2;
      break;
    case 11: // up-arrow
      buf[0] = 27;
      buf[1] = 'A';
      *len = 2;
      break;
    case 15: // F-9
      buf[0] = 27;
      break;
    default:
      break;
  }
}
