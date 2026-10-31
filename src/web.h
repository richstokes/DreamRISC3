/* Web page fetch and layout for !Web. The work happens on a thread of its own,
 * so like net.h this stays free of cpu.h and never touches machine state. The
 * guest reads finished pages, which swap in whole when web_status is called. */
#ifndef WEB_H
#define WEB_H
#include <stddef.h>
#include <stdint.h>

#define WEB_COLUMNS 76

enum { WEB_IDLE, WEB_BUSY, WEB_DONE, WEB_FAILED };
enum { STYLE_TEXT, STYLE_BOLD, STYLE_LINK, STYLE_EM, STYLE_CODE, STYLE_FIELD };

/* Shared with BASIC */
typedef struct {
	char title[64];
	char url[128];
	char message[48];
} web_info_t;

void web_open(const char *url);
void web_back(void);
int web_status(int *lines, int *bytes);
void web_info(web_info_t *info);
int web_line(int line, uint8_t *out, int size); /* bytes of runs, see web.c */
int web_click(int line, int column);            /* 1 went somewhere, 2 a text box */
void web_field_get(char *text, int size);
void web_field_set(const char *text);           /* also submits its form */

/* A connection: TCP on both platforms, TLS only where the platform has it */
typedef struct web_tls web_tls_t;
web_tls_t *plat_tls_open(int fd, const char *host);
int plat_tls_read(web_tls_t *tls, void *data, int len);  /* -35 means try again */
int plat_tls_write(web_tls_t *tls, const void *data, int len);
void plat_tls_close(web_tls_t *tls);
void plat_thread_start(void *(*fn)(void *));
void plat_sleep_ms(int ms);
uint64_t plat_wall_ms(void);
#endif
