#include "wpm_common.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#define GRN "\033[32m"
#define RED "\033[31m"
#define YEL "\033[33m"
#define DIM "\033[90m"
#define BLD "\033[1m"
#define RST "\033[0m"
#define CLEAR "\033[2J\033[H"
#define HIDE "\033[?25l"
#define SHOW "\033[?25h"
#define CLR "\033[2K"
#define MV(r, c) printf("\033[%d;%dH", (r), (c))

typedef enum { PENDING, CORRECT, INCORRECT } cstate_t;

static int driver_fd = -1;
static const char *text = NULL;
static int text_len = 0;
static cstate_t *cstate = NULL;
static int cursor = 0;
static volatile int done = 0;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int quit_pipe[2] = {-1, -1};

static struct termios orig;
static void raw_on(void) {
  struct termios t;
  tcgetattr(0, &orig);
  t = orig;
  t.c_lflag &= ~(ECHO | ICANON);
  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;
  tcsetattr(0, TCSAFLUSH, &t);
}
static void raw_off(void) { tcsetattr(0, TCSAFLUSH, &orig); }
static void cleanup(void) {
  raw_off();
  printf(SHOW RST "\n");
}
static int term_w(void) {
  struct winsize w;
  ioctl(1, TIOCGWINSZ, &w);
  return w.ws_col > 0 ? w.ws_col : 80;
}

static void read_stats(int *wpm, int *rwpm, int *cc, int *mc) {
  *wpm = *rwpm = *cc = *mc = 0;
  FILE *f = fopen("/proc/wpm_stats", "r");
  if (!f)
    return;
  char line[128], k[64];
  int v;
  while (fgets(line, sizeof(line), f))
    if (sscanf(line, "%63[^:]: %d", k, &v) == 2) {
      if (!strcmp(k, "wpm"))
        *wpm = v;
      else if (!strcmp(k, "raw_wpm"))
        *rwpm = v;
      else if (!strcmp(k, "correct_chars"))
        *cc = v;
      else if (!strcmp(k, "missed_chars"))
        *mc = v;
    }
  fclose(f);
}

static void draw_test(void) {
  int w = term_w();
  int wpm, rwpm, cc, mc;
  read_stats(&wpm, &rwpm, &cc, &mc);
  int tc = cc + mc;
  float acc = tc > 0 ? (cc * 100.0f) / tc : 100.0f;

  MV(1, 1);
  printf(CLR);
  printf(DIM "wpm " RST BLD "%d" RST DIM "  raw " RST BLD "%d" RST DIM
             "  acc " RST BLD "%s%.0f%%" RST DIM "  chars " RST BLD "%d/%d" RST,
         wpm, rwpm, acc >= 90 ? GRN : YEL, acc, cc, tc);

  MV(2, 1);
  printf(CLR);

  pthread_mutex_lock(&lock);
  int cur = cursor;
  pthread_mutex_unlock(&lock);

  int mar = 4, mw = w - (mar * 2), cl = 0, col = 0;
  for (int i = 0; i < cur; i++) {
    if (++col >= mw) {
      cl++;
      col = 0;
    }
  }
  int scroll = cl > 1 ? cl - 1 : 0, curl = 0;
  col = 0;

  MV(3, mar + 1);
  for (int i = 0; i < text_len; i++) {
    if (col >= mw) {
      curl++;
      col = 0;
      if (curl - scroll >= 3)
        break;
      MV(3 + (curl - scroll), mar + 1);
    }
    if (curl < scroll) {
      col++;
      continue;
    }
    char c = text[i];
    if (i == cur)
      printf("\033[7m%c\033[0m", c == ' ' ? '_' : c);
    else if (cstate[i] == CORRECT)
      printf(GRN "%c" RST, c);
    else if (cstate[i] == INCORRECT)
      printf(RED "%c" RST, c);
    else
      printf(DIM "%c" RST, c);
    col++;
  }
  for (int l = curl - scroll + 1; l < 4; l++) {
    MV(3 + l, 1);
    printf(CLR);
  }

  MV(7, 1);
  printf(CLR DIM "ctrl+q to quit" RST);
  fflush(stdout);
}

static void draw_results(void) {
  printf(CLEAR);
  int wpm, rwpm, cc, mc;
  read_stats(&wpm, &rwpm, &cc, &mc);
  int tc = cc + mc;
  float acc = tc > 0 ? (cc * 100.0f) / tc : 100.0f;
  int pass = acc >= 90.0f;

  printf("\n");
  printf("  " BLD "%s%.0f%%" RST "  acc\n", acc >= 90 ? GRN : YEL, acc);
  printf("  " BLD "%d" RST "    wpm\n", wpm);
  printf("  " BLD "%d" RST "    raw wpm\n", rwpm);
  printf("  " BLD GRN "%d" RST "    correct\n", cc);
  printf("  " BLD RED "%d" RST "    missed\n", mc);
  printf("\n");

  printf("  ");
  for (int i = 0; i < text_len; i++) {
    if (cstate[i] == CORRECT)
      printf(GRN "%c" RST, text[i]);
    else if (cstate[i] == INCORRECT)
      printf(RED "%c" RST, text[i]);
    else
      printf(DIM "%c" RST, text[i]);
  }
  printf("\n\n");

  printf("  %s\n\n",
         pass ? GRN "pass" RST : YEL "fail — below 90%% accuracy" RST);
  printf(DIM "  press any key...\n" RST);
  fflush(stdout);
}

static void *writer_thread(void *arg) {
  (void)arg;
  for (int i = 0; i <= text_len; i++) {
    expected_char_t ec = {.index = i, .ch = i < text_len ? text[i] : '\0'};
    if (write(driver_fd, &ec, sizeof(ec)) < 0) {
      perror("write");
      break;
    }
    if (ec.ch == '\0')
      break;
  }
  return NULL;
}

static void *reader_thread(void *arg) {
  (void)arg;
  while (!done) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(driver_fd, &rfds);
    FD_SET(quit_pipe[0], &rfds);
    int maxfd = driver_fd > quit_pipe[0] ? driver_fd : quit_pipe[0];
    if (select(maxfd + 1, &rfds, NULL, NULL, NULL) < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (FD_ISSET(quit_pipe[0], &rfds))
      break;
    if (FD_ISSET(driver_fd, &rfds)) {
      keystroke_result_t r;
      if (read(driver_fd, &r, sizeof(r)) < 0)
        break;
      pthread_mutex_lock(&lock);
      if (r.typed == '\b') {
        if (r.index >= 0 && r.index < text_len)
          cstate[r.index] = PENDING;
        if (cursor > 0)
          cursor = r.index;
      } else {
        if (r.index >= 0 && r.index < text_len)
          cstate[r.index] = r.correct ? CORRECT : INCORRECT;
        if (r.correct)
          cursor = r.index + 1;
        if (r.expected == '\0' || cursor >= text_len)
          done = 1;
      }
      pthread_mutex_unlock(&lock);
    }
  }
  return NULL;
}

static void *display_thread(void *arg) {
  (void)arg;
  while (!done) {
    draw_test();
    usleep(100000);
  }
  return NULL;
}

int main(int argc, char *argv[]) {
  static char fbuf[65536];
  text = "the quick brown fox jumps over the lazy dog. does this work. i hope "
         "it does? let's see";

  if (argc > 1) {
    FILE *f = fopen(argv[1], "r");
    if (f) {
      int n = fread(fbuf, 1, sizeof(fbuf) - 1, f);
      fclose(f);
      fbuf[n] = '\0';
      for (int i = 0; i < n; i++) {
        if (fbuf[i] == '\n' || fbuf[i] == '\r')
          fbuf[i] = ' ';
        if (fbuf[i] == '\0' && i < n - 1)
          fbuf[i] = ' ';
      }
      int end = n - 1;
      while (end > 0 &&
             (fbuf[end] == ' ' || fbuf[end] == '\n' || fbuf[end] == '\r'))
        fbuf[end--] = '\0';
      text = fbuf;
    }
  }
  text_len = strlen(text);
  cstate = calloc(text_len, sizeof(cstate_t));

  driver_fd = open(DEVICE, O_RDWR);
  if (driver_fd < 0) {
    fprintf(stderr, "Cannot open %s: %s\n", DEVICE, strerror(errno));
    free(cstate);
    return 1;
  }

  if (pipe(quit_pipe) < 0) {
    perror("pipe");
    return 1;
  }

  ioctl(driver_fd, WPM_RESET);
  ioctl(driver_fd, WPM_START);

  raw_on();
  atexit(cleanup);
  printf(HIDE CLEAR);

  int fl = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, fl | O_NONBLOCK);

  pthread_t tw, tr, td;
  pthread_create(&tw, NULL, writer_thread, NULL);
  pthread_create(&tr, NULL, reader_thread, NULL);
  pthread_create(&td, NULL, display_thread, NULL);

  while (!done) {
    char k;
    if (read(STDIN_FILENO, &k, 1) > 0 && (k == 17 || k == 3))
      break;
    usleep(50000);
  }
  done = 1;
  write(quit_pipe[1], "q", 1);

  pthread_join(td, NULL);
  pthread_join(tr, NULL);
  pthread_join(tw, NULL);

  draw_results();

  usleep(200000);
  fcntl(STDIN_FILENO, F_SETFL, fl | O_NONBLOCK);
  {
    char d[64];
    while (read(STDIN_FILENO, d, sizeof(d)) > 0)
      ;
  }
  fcntl(STDIN_FILENO, F_SETFL, fl);
  {
    char d;
    read(STDIN_FILENO, &d, 1);
  }

  ioctl(driver_fd, WPM_RESET);
  close(quit_pipe[0]);
  close(quit_pipe[1]);
  pthread_mutex_destroy(&lock);
  close(driver_fd);
  free(cstate);
  printf(CLEAR);
  return 0;
}
