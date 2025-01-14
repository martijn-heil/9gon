/*
 * bcm2385 framebuffer
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#define	Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

enum {
	Tabstop		= 4,
	Scroll		= 8,
	Wid		= 1024,
	Ht		= 768,
	Depth		= 16,
};

Memimage *gscreen;

static Memdata xgdata;

static Memimage xgscreen =
{
	{ 0, 0, Wid, Ht },	/* r */
	{ 0, 0, Wid, Ht },	/* clipr */
	Depth,			/* depth */
	3,			/* nchan */
	RGB16,			/* chan */
	nil,			/* cmap */
	&xgdata,		/* data */
	0,			/* zero */
	0, 			/* width in words of a single scan line */
	0,			/* layer */
	0,			/* flags */
};

static Memimage *conscol;
static Memimage *back;
static Memsubfont *memdefont;

static Lock screenlock;

static Point	curpos;
static int	h, w;
static Rectangle window;

static void myscreenputs(char *s, int n);
static void screenputc(char *buf);
static void screenwin(void);

/* called from devmouse */

void
cursoron(void)
{
	swcursorhide(0);
	swcursordraw(mousexy());
}

void
cursoroff(void)
{
	swcursorhide(0);
}

void
setcursor(Cursor* curs)
{
	swcursorload(curs);
}

int
hwdraw(Memdrawparam *par)
{
	Memimage *dst, *src, *mask;

	if((dst = par->dst) == nil || dst->data == nil)
		return 0;
	if((src = par->src) && src->data == nil)
		src = nil;
	if((mask = par->mask) && mask->data == nil)
		mask = nil;

	if(dst->data->bdata == xgdata.bdata)
		swcursoravoid(par->r);
	if(src && src->data->bdata == xgdata.bdata)
		swcursoravoid(par->sr);
	if(mask && mask->data->bdata == xgdata.bdata)
		swcursoravoid(par->mr);

	return 0;
}

static int
screensize(void)
{
	char *p;
	char *f[3];
	int width, height, depth;

	p = getconf("vgasize");
	if(p == nil || getfields(p, f, nelem(f), 0, "x") != nelem(f) ||
	    (width = atoi(f[0])) < 16 || (height = atoi(f[1])) <= 0 ||
	    (depth = atoi(f[2])) <= 0)
		return -1;
	xgscreen.r.max = Pt(width, height);
	xgscreen.depth = depth;
	return 0;
}

void
screeninit(void)
{
	uchar *fb;
	int set;
	ulong chan;

	set = screensize() == 0;
	fb = fbinit(set, &xgscreen.r.max.x, &xgscreen.r.max.y, &xgscreen.depth);
	if(fb == nil){
		print("can't initialise %dx%dx%d framebuffer \n",
			xgscreen.r.max.x, xgscreen.r.max.y, xgscreen.depth);
		return;
	}
	xgscreen.clipr = xgscreen.r;
	switch(xgscreen.depth){
	default:
		print("unsupported screen depth %d\n", xgscreen.depth);
		xgscreen.depth = 16;
		/* fall through */
	case 16:
		chan = RGB16;
		break;
	case 24:
		chan = RGB24;
		break;
	case 32:
		chan = XRGB32;
		break;
	}
	memsetchan(&xgscreen, chan);
	conf.monitor = 1;
	xgdata.bdata = fb;
	xgdata.ref = 1;
	gscreen = &xgscreen;
	gscreen->width = wordsperline(gscreen->r, gscreen->depth);

	memimageinit();
	memdefont = getmemdefont();
	screenwin();
	myscreenputs(kmesg.buf, kmesg.n);
	screenputs = myscreenputs;
	swcursorinit();
}

void
flushmemscreen(Rectangle)
{
}

Memdata*
attachscreen(Rectangle *r, ulong *chan, int* d, int *width, int *softscreen)
{
	if(gscreen == nil)
		return nil;

	*r = gscreen->r;
	*d = gscreen->depth;
	*chan = gscreen->chan;
	*width = gscreen->width;
	*softscreen = 0;

	gscreen->data->ref++;
	return gscreen->data;
}

void
getcolor(ulong p, ulong *pr, ulong *pg, ulong *pb)
{
	USED(p, pr, pg, pb);
}

int
setcolor(ulong p, ulong r, ulong g, ulong b)
{
	USED(p, r, g, b);
	return 0;
}

void
blankscreen(int blank)
{
	fbblank(blank);
}

static void
myscreenputs(char *s, int n)
{
	int i;
	Rune r;
	char buf[4];

	if(!islo()) {
		/* don't deadlock trying to print in interrupt */
		if(!canlock(&screenlock))
			return;	
	}
	else {
		while(!canlock(&screenlock))
			;
	}

	while(n > 0){
		i = chartorune(&r, s);
		if(i == 0){
			s++;
			--n;
			continue;
		}
		memmove(buf, s, i);
		buf[i] = 0;
		n -= i;
		s += i;
		screenputc(buf);
	}
	unlock(&screenlock);
}

static void
screenwin(void)
{
	char *greet;
	Memimage *orange;
	Point p, q;
	Rectangle r;

	back = memwhite;
	conscol = memblack;

	orange = allocmemimage(Rect(0, 0, 1, 1), RGB16);
	orange->flags |= Frepl;
	orange->clipr = gscreen->r;
	orange->data->bdata[0] = 0x40;		/* magic: colour? */
	orange->data->bdata[1] = 0xfd;		/* magic: colour? */

	w = memdefont->info[' '].width;
	h = memdefont->height;

	r = insetrect(gscreen->r, 4);

	memimagedraw(gscreen, r, memblack, ZP, memopaque, ZP, S);
	window = insetrect(r, 4);
	memimagedraw(gscreen, window, memwhite, ZP, memopaque, ZP, S);

	memimagedraw(gscreen, Rect(window.min.x, window.min.y,
		window.max.x, window.min.y + h + 5 + 6), orange, ZP, nil, ZP, S);
	freememimage(orange);
	window = insetrect(window, 5);

	greet = " Plan 9 Console ";
	p = addpt(window.min, Pt(10, 0));
	q = memsubfontwidth(memdefont, greet);
	memimagestring(gscreen, p, conscol, ZP, memdefont, greet);
	flushmemscreen(r);
	window.min.y += h + 6;
	curpos = window.min;
	window.max.y = window.min.y + ((window.max.y - window.min.y) / h) * h;
}

static void
scroll(void)
{
	int o;
	Point p;
	Rectangle r;

	o = Scroll*h;
	r = Rpt(window.min, Pt(window.max.x, window.max.y-o));
	p = Pt(window.min.x, window.min.y+o);
	memimagedraw(gscreen, r, gscreen, p, nil, p, S);
	flushmemscreen(r);
	r = Rpt(Pt(window.min.x, window.max.y-o), window.max);
	memimagedraw(gscreen, r, back, ZP, nil, ZP, S);
	flushmemscreen(r);

	curpos.y -= o;
}

static void
screenputc(char *buf)
{
	int w;
	uint pos;
	Point p;
	Rectangle r;
	static int *xp;
	static int xbuf[256];

	if (xp < xbuf || xp >= &xbuf[nelem(xbuf)])
		xp = xbuf;

	switch (buf[0]) {
	case '\n':
		if (curpos.y + h >= window.max.y)
			scroll();
		curpos.y += h;
		screenputc("\r");
		break;
	case '\r':
		xp = xbuf;
		curpos.x = window.min.x;
		break;
	case '\t':
		p = memsubfontwidth(memdefont, " ");
		w = p.x;
		if (curpos.x >= window.max.x - Tabstop * w)
			screenputc("\n");

		pos = (curpos.x - window.min.x) / w;
		pos = Tabstop - pos % Tabstop;
		*xp++ = curpos.x;
		r = Rect(curpos.x, curpos.y, curpos.x + pos * w, curpos.y + h);
		memimagedraw(gscreen, r, back, back->r.min, nil, back->r.min, S);
		flushmemscreen(r);
		curpos.x += pos * w;
		break;
	case '\b':
		if (xp <= xbuf)
			break;
		xp--;
		r = Rect(*xp, curpos.y, curpos.x, curpos.y + h);
		memimagedraw(gscreen, r, back, back->r.min, nil, back->r.min, S);
		flushmemscreen(r);
		curpos.x = *xp;
		break;
	case '\0':
		break;
	default:
		p = memsubfontwidth(memdefont, buf);
		w = p.x;

		if (curpos.x >= window.max.x - w)
			screenputc("\n");

		*xp++ = curpos.x;
		r = Rect(curpos.x, curpos.y, curpos.x + w, curpos.y + h);
		memimagedraw(gscreen, r, back, back->r.min, nil, back->r.min, S);
		memimagestring(gscreen, curpos, conscol, ZP, memdefont, buf);
		flushmemscreen(r);
		curpos.x += w;
		break;
	}
}
