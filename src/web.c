/* HTTP(S) fetch and HTML layout. A fixed width text grid suits the system font and
 * 4MB of guest RAM: a page becomes lines of styled runs, and !Web just draws the
 * ones in view. No images, CSS or scripts. Everything here runs on the fetch thread
 * except the web_* entry points, which only swap and read finished pages. */
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "net.h"
#include "web.h"

#define BODY_SIZES {96 * 1024, 64 * 1024, 32 * 1024} /* pages longer are cut short */
#define MAX_TEXT (48 * 1024)
#define MAX_LINES 3072
#define MAX_LINKS 1024
#define LINK_POOL (16 * 1024)
#define MAX_URL 256
#define MAX_REDIRECTS 5
#define HISTORY 8
#define TIMEOUT_MS 30000
#define RETRY_MS 10
#define TRY_AGAIN 35
#define ERR_INPROGRESS 36
#define ERR_ALREADY 37
#define ERR_ISCONN 56
#define ERR_NOTCONN 57
#define NO_LINK 0xFFFF
#define RUN_HEADER 5 /* column, length, style, link high, link low; then the text */
#define MAX_RUN 200

typedef struct {
	uint8_t *text;
	uint32_t *line_at;
	int lines;
	uint16_t *link_at;
	char *link_pool;
	int links;
	char title[64];
	char url[MAX_URL];
} page_t;

static const char home_page[] =
	"<title>DreamRISC Browser</title><h1>DreamRISC Browser</h1>"
	"<p>A small web browser for RISC OS 3.11 on the Dreamcast. Type an address in the bar "
	"above and press Return.</p><h2>Try these</h2><ul>"
	"<li><a href=\"http://info.cern.ch/\">The first website</a>"
	"<li><a href=\"https://text.npr.org/\">NPR text news</a>"
	"<li><a href=\"https://lite.cnn.com/\">CNN lite</a>"
	"<li><a href=\"https://en.wikipedia.org/wiki/RISC_OS\">RISC OS on Wikipedia</a>"
	"</ul><h2>Good to know</h2><p>Pages are text only: no pictures, styles or scripts, "
	"and no forms. <b>Secure sites are encrypted but not verified</b>, so don't type "
	"anything private.</p><pre>Long pages are cut off after about 96KB.</pre>";

static page_t *shown, *ready;
static volatile int state = WEB_IDLE;
static volatile int want;
static char want_url[MAX_URL];
static int want_keep_history;
static char message[48];
static int body_bytes;
static char history[HISTORY][MAX_URL];
static int history_used;
static volatile int started;

static void set_message(const char *text)
{
	snprintf(message, sizeof(message), "%s", text);
}

/* --- layout --- */

static page_t *page_new(void)
{
	page_t *p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;
	p->text = malloc(MAX_TEXT);
	p->line_at = malloc((MAX_LINES + 1) * sizeof(uint32_t));
	p->link_at = malloc(MAX_LINKS * sizeof(uint16_t));
	p->link_pool = malloc(LINK_POOL);
	if (!p->text || !p->line_at || !p->link_at || !p->link_pool) {
		free(p->text);
		free(p->line_at);
		free(p->link_at);
		free(p->link_pool);
		free(p);
		return NULL;
	}
	return p;
}

static void page_free(page_t *p)
{
	if (!p)
		return;
	free(p->text);
	free(p->line_at);
	free(p->link_at);
	free(p->link_pool);
	free(p);
}

/* Trim to what the page used, so a short page doesn't hold it all */
static void page_trim(page_t *p, uint32_t used)
{
	uint8_t *smaller = realloc(p->text, used ? used : 1);
	if (smaller)
		p->text = smaller;
}

typedef struct {
	page_t *p;
	uint32_t used, pool_used;
	int column, indent, style, link;
	int run_at;     /* offset of the open run's header, or -1 */
	int blank;      /* the last line was empty, or nothing has been written */
	int space;      /* a space is due before the next word */
	int full;
} layout_t;

static void line_end(layout_t *l)
{
	if (l->p->lines >= MAX_LINES || l->used + WEB_COLUMNS + RUN_HEADER * 8 >= MAX_TEXT) {
		l->full = 1;
		return;
	}
	l->blank = l->column == 0;
	l->p->lines++;
	l->p->line_at[l->p->lines] = l->used;
	l->column = 0;
	l->run_at = -1;
	l->space = 0;
}

static void break_line(layout_t *l)
{
	if (l->column)
		line_end(l);
	l->space = 0;
}

static void paragraph(layout_t *l)
{
	break_line(l);
	if (!l->blank)
		line_end(l);
}

static void put_char(layout_t *l, uint8_t c)
{
	uint8_t *t = l->p->text;
	if (l->full)
		return;
	if (l->column == 0 && l->indent) {
		l->column = l->indent;
		l->run_at = -1;
	}
	if (l->run_at >= 0 && (t[l->run_at + 2] != l->style ||
	    ((t[l->run_at + 3] << 8) | t[l->run_at + 4]) != (l->link < 0 ? NO_LINK : l->link) ||
	    t[l->run_at + 1] >= MAX_RUN))
		l->run_at = -1;
	if (l->run_at < 0) {
		int link = l->link < 0 ? NO_LINK : l->link;
		l->run_at = (int)l->used;
		t[l->used++] = (uint8_t)l->column;
		t[l->used++] = 0;
		t[l->used++] = (uint8_t)l->style;
		t[l->used++] = (uint8_t)(link >> 8);
		t[l->used++] = (uint8_t)link;
	}
	t[l->used++] = c;
	t[l->run_at + 1]++;
	l->column++;
	l->blank = 0;
}

static void put_word(layout_t *l, const uint8_t *word, int len)
{
	int room = WEB_COLUMNS - l->indent;
	if (l->column && l->column + (l->space ? 1 : 0) + len > WEB_COLUMNS)
		line_end(l);
	if (l->space && l->column > l->indent)
		put_char(l, ' ');
	l->space = 0;
	while (len > 0) {
		int n = len > room ? room : len, i;
		for (i = 0; i < n; i++)
			put_char(l, word[i]);
		word += n;
		len -= n;
		if (len)
			line_end(l);
	}
}

/* Accumulates text until a space or a tag */
typedef struct { uint8_t data[WEB_COLUMNS]; int len; } word_t;

static void word_flush(layout_t *l, word_t *w)
{
	if (w->len)
		put_word(l, w->data, w->len);
	w->len = 0;
}

static void word_add(layout_t *l, word_t *w, uint8_t c)
{
	if (w->len == (int)sizeof(w->data))
		word_flush(l, w);
	w->data[w->len++] = c;
}

static int entity(const char *s, const char *end, unsigned *out)
{
	static const struct { const char *name; unsigned code; } names[] = {
		{"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 160},
		{"copy", 169}, {"reg", 174}, {"pound", 163}, {"mdash", 0x2014}, {"ndash", 0x2013},
		{"hellip", 0x2026}, {"rsquo", 0x2019}, {"lsquo", 0x2018}, {"ldquo", 0x201C},
		{"rdquo", 0x201D}, {"middot", 183}, {"bull", 0x2022}, {"eacute", 233}, {"egrave", 232},
	};
	const char *p = s + 1;
	unsigned i, code = 0;
	if (p < end && *p == '#') {
		int hex = ++p < end && (*p == 'x' || *p == 'X');
		if (hex)
			p++;
		for (; p < end && *p != ';' && p - s < 10; p++) {
			int d = *p >= '0' && *p <= '9' ? *p - '0' : (*p | 32) >= 'a' && (*p | 32) <= 'f' ? (*p | 32) - 'a' + 10 : -1;
			if (d < 0 || (!hex && d > 9))
				return 0;
			code = code * (hex ? 16 : 10) + (unsigned)d;
		}
		if (p >= end || *p != ';')
			return 0;
		*out = code;
		return (int)(p + 1 - s);
	}
	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		size_t n = strlen(names[i].name);
		if ((size_t)(end - p) > n && !strncmp(p, names[i].name, n) && p[n] == ';') {
			*out = names[i].code;
			return (int)n + 2;
		}
	}
	return 0;
}

/* RISC OS is Latin-1. Typography folds to ASCII, anything else becomes a ? */
static void put_code(layout_t *l, word_t *w, unsigned code)
{
	switch (code) {
	case 0x2018: case 0x2019: code = '\''; break;
	case 0x201C: case 0x201D: code = '"'; break;
	case 0x2013: case 0x2014: code = '-'; break;
	case 0x2022: code = '*'; break;
	case 0x2026: word_add(l, w, '.'); word_add(l, w, '.'); code = '.'; break;
	default: break;
	}
	if (code == 160)
		code = ' ';
	word_add(l, w, code < 256 ? (uint8_t)code : '?');
}

/* One UTF-8 sequence, or a lone byte taken as Latin-1. Returns bytes used. */
static int decode_utf8(const uint8_t *s, const uint8_t *end, unsigned *out)
{
	int extra = s[0] >= 0xF0 ? 3 : s[0] >= 0xE0 ? 2 : s[0] >= 0xC0 ? 1 : 0, i;
	unsigned code = s[0] & (0x3F >> extra);
	if (!extra || s + extra >= end) {
		*out = s[0];
		return 1;
	}
	for (i = 1; i <= extra; i++) {
		if ((s[i] & 0xC0) != 0x80) {
			*out = s[0];
			return 1;
		}
		code = (code << 6) | (s[i] & 0x3F);
	}
	*out = code;
	return extra + 1;
}

static int add_link(layout_t *l, const char *url, int len)
{
	page_t *p = l->p;
	if (p->links >= MAX_LINKS || l->pool_used + (uint32_t)len + 1 > LINK_POOL)
		return -1;
	p->link_at[p->links] = (uint16_t)l->pool_used;
	memcpy(p->link_pool + l->pool_used, url, (size_t)len);
	l->pool_used += (uint32_t)len;
	p->link_pool[l->pool_used++] = 0;
	return p->links++;
}

/* The value of attr in a tag's text, unquoted. Returns its length, or -1. */
static int attribute(const char *tag, const char *end, const char *attr, const char **value)
{
	size_t n = strlen(attr);
	const char *p;
	for (p = tag; p + n < end; p++) {
		char q;
		const char *v;
		if (strncasecmp(p, attr, n) || (p[-1] != ' ' && p[-1] != '\n' && p[-1] != '\t'))
			continue;
		v = p + n;
		while (v < end && *v == ' ')
			v++;
		if (v >= end || *v != '=')
			continue;
		for (v++; v < end && *v == ' '; v++)
			;
		q = v < end && (*v == '"' || *v == '\'') ? *v++ : 0;
		*value = v;
		while (v < end && (q ? *v != q : *v != ' ' && *v != '>'))
			v++;
		return (int)(v - *value);
	}
	return -1;
}

static int tag_is(const char *name, int len, const char *want_name)
{
	return (int)strlen(want_name) == len && !strncasecmp(name, want_name, (size_t)len);
}

static int tag_in(const char *name, int len, const char *const *list)
{
	for (; *list; list++)
		if (tag_is(name, len, *list))
			return 1;
	return 0;
}

/* Past closing and the > that ends it, if it's a tag still open */
static const char *skip_past(const char *s, const char *end, const char *closing)
{
	size_t n = strlen(closing);
	for (; s + n <= end; s++) {
		if (strncasecmp(s, closing, n))
			continue;
		s += n;
		if (closing[n - 1] != '>')
			while (s < end && *s++ != '>')
				;
		return s;
	}
	return end;
}

static void layout_html(page_t *p, const char *s, const char *end)
{
	static const char *const paragraphs[] = {"p", "div", "section", "article", "header", "footer",
		"main", "nav", "aside", "form", "table", "ul", "ol", "dl", "blockquote", "figure", NULL};
	static const char *const breaks[] = {"br", "tr", "dt", "dd", NULL};
	static const char *const skipped[] = {"script", "style", "svg", "noscript", "select",
		"textarea", "iframe", NULL};
	static const char *const skipped_end[] = {"</script", "</style", "</svg", "</noscript",
		"</select", "</textarea", "</iframe"};
	layout_t l;
	word_t w;
	int pre = 0, list_depth = 0, in_title = 0, title_len = 0, heading = 0, emphasis = 0, strong = 0;
	memset(&l, 0, sizeof(l));
	w.len = 0;
	p->line_at[0] = 0;
	l.p = p;
	l.link = -1;
	l.run_at = -1;
	l.blank = 1;
	while (s < end && !l.full) {
		if (*s == '<') {
			const char *name, *close = memchr(s, '>', (size_t)(end - s));
			int ending, len;
			if (!strncmp(s, "<!--", 4)) {
				s = skip_past(s, end, "-->");
				continue;
			}
			if (!close)
				break;
			name = s + 1;
			ending = *name == '/';
			name += ending;
			for (len = 0; name + len < close && name[len] != ' ' && name[len] != '/' &&
			     name[len] != '\n' && name[len] != '\t'; len++)
				;
			word_flush(&l, &w);
			if (!ending) {
				int i;
				for (i = 0; skipped[i]; i++)
					if (tag_is(name, len, skipped[i]))
						break;
				if (skipped[i]) {
					s = skip_past(close, end, skipped_end[i]);
					continue;
				}
			}
			if (tag_is(name, len, "title")) {
				in_title = !ending;
			} else if (tag_in(name, len, paragraphs)) {
				paragraph(&l);
				if (tag_is(name, len, "ul") || tag_is(name, len, "ol") ||
				    tag_is(name, len, "blockquote")) {
					list_depth += ending ? -1 : 1;
					if (list_depth < 0)
						list_depth = 0;
					l.indent = (list_depth > 6 ? 6 : list_depth) * 2;
				}
			} else if (tag_in(name, len, breaks)) {
				break_line(&l);
			} else if (tag_is(name, len, "td") || tag_is(name, len, "th")) {
				l.space = 1;
			} else if (tag_is(name, len, "li")) {
				break_line(&l);
				if (!ending) {
					int saved = l.indent;
					l.indent = saved > 2 ? saved - 2 : 0;
					put_word(&l, (const uint8_t *)"* ", 2);
					l.indent = saved;
				}
			} else if (len == 2 && (name[0] | 32) == 'h' && name[1] >= '1' && name[1] <= '6') {
				paragraph(&l);
				heading = !ending;
			} else if (tag_is(name, len, "hr")) {
				static const uint8_t rule[40] = "----------------------------------------";
				paragraph(&l);
				put_word(&l, rule, sizeof(rule));
				paragraph(&l);
			} else if (tag_is(name, len, "pre")) {
				paragraph(&l);
				pre = !ending;
			} else if (tag_is(name, len, "b") || tag_is(name, len, "strong")) {
				strong = !ending;
			} else if (tag_is(name, len, "i") || tag_is(name, len, "em") || tag_is(name, len, "cite")) {
				emphasis = !ending;
			} else if (tag_is(name, len, "a")) {
				if (ending) {
					l.link = -1;
				} else {
					const char *href;
					int n = attribute(name, close, "href", &href);
					l.link = n > 0 ? add_link(&l, href, n) : -1;
				}
			} else if (tag_is(name, len, "img") && !ending) {
				const char *alt;
				int n = attribute(name, close, "alt", &alt);
				if (n > 0 && n < WEB_COLUMNS - 2) {
					uint8_t text[WEB_COLUMNS];
					text[0] = '[';
					memcpy(text + 1, alt, (size_t)n);
					text[n + 1] = ']';
					put_word(&l, text, n + 2);
					l.space = 1;
				}
			}
			l.style = l.link >= 0 ? STYLE_LINK : heading || strong ? STYLE_BOLD :
				pre ? STYLE_CODE : emphasis ? STYLE_EM : STYLE_TEXT;
			s = close + 1;
			continue;
		}
		if (in_title) {
			unsigned code = (unsigned char)*s;
			int used = *s == '&' ? entity(s, end, &code) : 0;
			s += used ? used : 1;
			if (title_len + 1 < (int)sizeof(p->title) && code >= ' ' && code < 256)
				p->title[title_len++] = (char)code;
			continue;
		}
		if (*s == '\n' || *s == '\r' || *s == ' ' || *s == '\t') {
			if (pre) {
				if (*s == '\n') {
					word_flush(&l, &w);
					line_end(&l);
				} else if (*s != '\r') {
					word_flush(&l, &w);
					put_char(&l, ' ');
				}
			} else {
				word_flush(&l, &w);
				l.space = 1;
			}
			s++;
			continue;
		}
		if (*s == '&') {
			unsigned code;
			int used = entity(s, end, &code);
			if (used) {
				put_code(&l, &w, code);
				s += used;
				continue;
			}
		}
		{
			unsigned code;
			s += decode_utf8((const uint8_t *)s, (const uint8_t *)end, &code);
			put_code(&l, &w, code);
		}
	}
	word_flush(&l, &w);
	break_line(&l);
	page_trim(p, l.used);
}

static void layout_text(page_t *p, const char *s, const char *end)
{
	char *wrapped = malloc((size_t)(end - s) + 12);
	if (!wrapped)
		return;
	memcpy(wrapped, "<pre>", 5);
	memcpy(wrapped + 5, s, (size_t)(end - s));
	{
		/* Plain text has no tags, so anything that looks like one is literal */
		char *c;
		for (c = wrapped + 5; c < wrapped + 5 + (end - s); c++)
			if (*c == '<')
				*c = '(';
	}
	layout_html(p, wrapped, wrapped + 5 + (end - s));
	free(wrapped);
}

/* --- addresses --- */

typedef struct { int tls, port; char host[96]; const char *path; } url_t;

static int url_parse(const char *text, url_t *u)
{
	const char *host = strstr(text, "://"), *end;
	size_t n;
	u->tls = !strncasecmp(text, "https:", 6);
	if (!host || (!u->tls && strncasecmp(text, "http:", 5)))
		return 0;
	host += 3;
	u->port = u->tls ? 443 : 80;
	u->path = strchr(host, '/');
	end = u->path ? u->path : host + strlen(host);
	if (!u->path)
		u->path = "/";
	n = (size_t)(end - host);
	if (n == 0 || n >= sizeof(u->host))
		return 0;
	memcpy(u->host, host, n);
	u->host[n] = 0;
	{
		char *colon = strchr(u->host, ':');
		if (colon) {
			*colon = 0;
			u->port = atoi(colon + 1);
		}
	}
	return 1;
}

/* The address a link or redirect points at, from the page it was found on */
static void url_join(char *out, const char *base, const char *link)
{
	const char *host, *path;
	size_t origin;
	char *cut;
	while (*link == ' ' || *link == '\n')
		link++;
	if (strstr(link, "://") && strcspn(link, ":") < strcspn(link, "/?#")) {
		snprintf(out, MAX_URL, "%s", link);
	} else if (!strncmp(link, "about:", 6)) {
		snprintf(out, MAX_URL, "%s", link);
	} else {
		host = strstr(base, "://");
		host = host ? host + 3 : base;
		path = strchr(host, '/');
		origin = path ? (size_t)(path - base) : strlen(base);
		if (!strncmp(link, "//", 2)) {
			size_t scheme = (size_t)(host - base) - 2;
			snprintf(out, MAX_URL, "%.*s%s", (int)scheme, base, link);
		} else if (*link == '/') {
			snprintf(out, MAX_URL, "%.*s%s", (int)origin, base, link);
		} else if (*link == '#' || !*link) {
			snprintf(out, MAX_URL, "%s", base);
		} else {
			char dir[MAX_URL], *slash;
			snprintf(dir, sizeof(dir), "%s", path ? path : "/");
			dir[strcspn(dir, "?#")] = 0;
			slash = strrchr(dir, '/');
			if (slash)
				slash[1] = 0;
			while (!strncmp(link, "../", 3)) {
				link += 3;
				if (slash && slash > dir) {
					*slash = 0;
					slash = strrchr(dir, '/');
					if (slash)
						slash[1] = 0;
				}
			}
			while (!strncmp(link, "./", 2))
				link += 2;
			snprintf(out, MAX_URL, "%.*s%s%s", (int)origin, base, dir, link);
		}
	}
	cut = strchr(out, '#');
	if (cut)
		*cut = 0;
}

/* --- fetch --- */

typedef struct { int fd; web_tls_t *tls; uint64_t deadline; } conn_t;

static int conn_expired(const conn_t *c)
{
	return plat_wall_ms() > c->deadline || want;
}

static int conn_open(conn_t *c, const url_t *u)
{
	struct addrinfo hints, *res = NULL;
	uint8_t ip[4];
	int rv;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	c->deadline = plat_wall_ms() + TIMEOUT_MS;
	c->tls = NULL;
	if (getaddrinfo(u->host, NULL, &hints, &res) || !res) {
		set_message("Could not find that site");
		return 0;
	}
	memcpy(ip, &((struct sockaddr_in *)res->ai_addr)->sin_addr, 4);
	freeaddrinfo(res);
	c->fd = net_sock_open(SOCK_KIND_TCP);
	if (c->fd < 0) {
		set_message("No network");
		return 0;
	}
	/* Asking again is the portable way to learn how a non-blocking connect went */
	for (;;) {
		rv = net_sock_connect(c->fd, ip, u->port);
		if (rv == 0 || rv == -ERR_ISCONN)
			break;
		if (rv != -ERR_INPROGRESS && rv != -ERR_ALREADY && rv != -TRY_AGAIN) {
			set_message("Could not connect");
			net_sock_close(c->fd);
			return 0;
		}
		if (conn_expired(c)) {
			set_message("The site did not answer");
			net_sock_close(c->fd);
			return 0;
		}
		plat_sleep_ms(RETRY_MS);
	}
	if (u->tls) {
		set_message("Setting up a secure link");
		c->tls = plat_tls_open(c->fd, u->host);
		if (!c->tls) {
			set_message("Secure link failed");
			net_sock_close(c->fd);
			return 0;
		}
	}
	return 1;
}

static int conn_write(conn_t *c, const char *data, int len)
{
	while (len > 0) {
		int n = c->tls ? plat_tls_write(c->tls, data, len) : net_sock_send(c->fd, data, len);
		if (n > 0) {
			data += n;
			len -= n;
		} else if ((n == -TRY_AGAIN || n == -ERR_NOTCONN) && !conn_expired(c)) {
			plat_sleep_ms(RETRY_MS);
		} else {
			return 0;
		}
	}
	return 1;
}

/* Bytes read, 0 at the end, -1 on failure */
static int conn_read(conn_t *c, char *data, int len)
{
	for (;;) {
		int n = c->tls ? plat_tls_read(c->tls, data, len) : net_sock_recv(c->fd, data, len);
		if (n >= 0)
			return n;
		if (n != -TRY_AGAIN || conn_expired(c))
			return -1;
		plat_sleep_ms(RETRY_MS);
	}
}

static void conn_close(conn_t *c)
{
	if (c->tls)
		plat_tls_close(c->tls);
	net_sock_close(c->fd);
}

static const char *header_value(const char *headers, const char *end, const char *name)
{
	size_t n = strlen(name);
	const char *line = headers;
	while (line && line < end) {
		if (!strncasecmp(line, name, n) && line[n] == ':') {
			line += n + 1;
			while (*line == ' ')
				line++;
			return line;
		}
		line = strstr(line, "\r\n");
		line = line ? line + 2 : NULL;
	}
	return NULL;
}

/* Removes chunk framing in place */
static int dechunk(char *body, int len)
{
	char *in = body, *end = body + len, *out = body;
	while (in < end) {
		char *line = strstr(in, "\r\n");
		unsigned long size;
		if (!line)
			break;
		size = strtoul(in, NULL, 16);
		in = line + 2;
		if (!size)
			break;
		if (size > (unsigned long)(end - in))
			size = (unsigned long)(end - in);
		memmove(out, in, size);
		out += size;
		in += size + 2;
	}
	return (int)(out - body);
}

/* 1 with a page in *p, 0 on failure, or 2 with *url updated for a redirect */
static int parse_reply(char *url, char *buffer, int got, page_t *p)
{
	char *body, *headers_end;
	const char *type, *value;
	int status, length, chunked, plain;
	buffer[got] = 0;
	headers_end = strstr(buffer, "\r\n\r\n");
	if (!headers_end || strncmp(buffer, "HTTP/1.", 7)) {
		set_message(got ? "That isn't a web page" : "The site closed the link");
		return 0;
	}
	status = atoi(buffer + 9);
	value = header_value(buffer, headers_end, "Location");
	if (status >= 300 && status < 400 && value) {
		char target[MAX_URL], base[MAX_URL];
		size_t len = strcspn(value, "\r\n");
		if (len >= sizeof(target))
			len = sizeof(target) - 1;
		memcpy(target, value, len);
		target[len] = 0;
		snprintf(base, sizeof(base), "%s", url);
		url_join(url, base, target);
		return 2;
	}
	type = header_value(buffer, headers_end, "Content-Type");
	if (type && strncasecmp(type, "text/", 5)) {
		set_message("That's not a text page");
		return 0;
	}
	plain = type && !strncasecmp(type, "text/plain", 10);
	value = header_value(buffer, headers_end, "Transfer-Encoding");
	chunked = value && !strncasecmp(value, "chunked", 7);
	body = headers_end + 4;
	length = got - (int)(body - buffer);
	if (chunked)
		length = dechunk(body, length);
	if (plain)
		layout_text(p, body, body + length);
	else
		layout_html(p, body, body + length);
	return 1;
}

static char *body_alloc(int *size)
{
	static const int sizes[] = BODY_SIZES;
	unsigned i;
	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		char *buffer = malloc((size_t)sizes[i]);
		if (buffer) {
			*size = sizes[i];
			return buffer;
		}
	}
	return NULL;
}

/* The same results as parse_reply */
static int fetch_once(char *url, page_t *p)
{
	url_t u;
	conn_t c;
	char request[MAX_URL + 256];
	char *buffer, *body, *headers_end;
	const char *type, *value;
	int got = 0, n, status, length, chunked, plain, size, result = 0;
	if (!url_parse(url, &u)) {
		set_message("That address isn't http or https");
		return 0;
	}
	set_message("Connecting");
	if (!conn_open(&c, &u))
		return 0;
	n = snprintf(request, sizeof(request),
		"GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: DreamRISC/1.0 (RISC OS 3.11; Dreamcast)\r\n"
		"Accept: text/html, text/plain\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
		u.path, u.host);
	/* Sockets and TLS take what they need first, as they can't make do with less */
	buffer = body_alloc(&size);
	if (!buffer) {
		set_message("Not enough memory");
		conn_close(&c);
		return 0;
	}
	set_message("Waiting for the site");
	if (!conn_write(&c, request, n)) {
		set_message("Could not send the request");
		conn_close(&c);
		free(buffer);
		return 0;
	}
	while (got < size - 1) {
		n = conn_read(&c, buffer + got, size - 1 - got);
		if (n <= 0)
			break;
		got += n;
		body_bytes = got;
		set_message("Receiving");
	}
	conn_close(&c);
	if (!want)
		result = parse_reply(url, buffer, got, p);
	free(buffer);
	return result;
}

static void publish(page_t *p, const char *url)
{
	snprintf(p->url, sizeof(p->url), "%s", url);
	if (!p->title[0])
		snprintf(p->title, sizeof(p->title), "%s", url);
	printf("web: %s, %d lines from %d bytes\n", url, p->lines, body_bytes);
	page_free(ready);
	ready = p;
	set_message("");
	state = WEB_DONE;
}

static void load(char *url)
{
	page_t *p = page_new();
	int tries, result = 0;
	body_bytes = 0;
	if (!p) {
		set_message("Not enough memory");
		printf("web: %s failed: %s\n", url, message);
		state = WEB_FAILED;
		return;
	}
	if (!strncmp(url, "about:", 6)) {
		layout_html(p, home_page, home_page + sizeof(home_page) - 1);
		publish(p, url);
		return;
	}
	for (tries = 0; tries <= MAX_REDIRECTS; tries++) {
		result = fetch_once(url, p);
		if (result != 2)
			break;
	}
	if (result == 2)
		set_message("Too many redirects");
	if (result == 1) {
		publish(p, url);
	} else {
		page_free(p);
		printf("web: %s failed: %s\n", url, message);
		state = want ? WEB_BUSY : WEB_FAILED;
	}
}

static void *fetch_thread(void *arg)
{
	(void)arg;
	for (;;) {
		if (want) {
			char url[MAX_URL];
			snprintf(url, sizeof(url), "%s", want_url);
			want = 0;
			load(url);
		}
		plat_sleep_ms(20);
	}
	return NULL;
}

/* --- what the guest calls --- */

static void request(const char *url, int keep_history)
{
	want_keep_history = keep_history;
	/* Built-in pages need no network, and this keeps harness replays deterministic */
	if (!strncmp(url, "about:", 6)) {
		char copy[MAX_URL];
		snprintf(copy, sizeof(copy), "%s", url);
		load(copy);
		return;
	}
	if (!started) {
		started = 1;
		plat_thread_start(fetch_thread);
	}
	snprintf(want_url, sizeof(want_url), "%s", url);
	state = WEB_BUSY;
	set_message("Starting");
	want = 1;
}

void web_open(const char *text)
{
	char url[MAX_URL];
	if (strstr(text, "://") || !strncmp(text, "about:", 6))
		snprintf(url, sizeof(url), "%s", text);
	else
		snprintf(url, sizeof(url), "http://%s", text);
	request(url, 1);
}

void web_back(void)
{
	if (history_used > 0)
		request(history[--history_used], 0);
}

int web_status(int *lines, int *bytes)
{
	int now = state;
	if (now == WEB_DONE && ready) {
		page_t *p = ready;
		ready = NULL;
		if (shown && want_keep_history) {
			if (history_used == HISTORY) {
				memmove(history[0], history[1], sizeof(history[0]) * (HISTORY - 1));
				history_used--;
			}
			snprintf(history[history_used++], MAX_URL, "%s", shown->url);
		}
		page_free(shown);
		shown = p;
	}
	*lines = shown ? shown->lines : 0;
	*bytes = body_bytes;
	return now;
}

void web_info(web_info_t *info)
{
	memset(info, 0, sizeof(*info));
	if (shown) {
		snprintf(info->title, sizeof(info->title), "%s", shown->title);
		snprintf(info->url, sizeof(info->url), "%s", shown->url);
	}
	snprintf(info->message, sizeof(info->message), "%s", message);
}

int web_line(int line, uint8_t *out, int size)
{
	int n;
	if (!shown || line < 0 || line >= shown->lines)
		return 0;
	n = (int)(shown->line_at[line + 1] - shown->line_at[line]);
	if (n > size)
		n = size;
	memcpy(out, shown->text + shown->line_at[line], (size_t)n);
	return n;
}

int web_click(int line, int column)
{
	const uint8_t *t, *end;
	if (!shown || line < 0 || line >= shown->lines)
		return 0;
	t = shown->text + shown->line_at[line];
	end = shown->text + shown->line_at[line + 1];
	while (t < end) {
		int link = (t[3] << 8) | t[4];
		if (column >= t[0] && column < t[0] + t[1] && link != NO_LINK) {
			char url[MAX_URL];
			url_join(url, shown->url, shown->link_pool + shown->link_at[link]);
			request(url, 1);
			return 1;
		}
		t += RUN_HEADER + t[1];
	}
	return 0;
}
