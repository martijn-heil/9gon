#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <event.h>
#include <keyboard.h>
#include "imagefile.h"

int		cflag = 0;
int		dflag = 0;
int		eflag = 0;
int		nineflag = 0;
int		threeflag = 0;
int		output = 0;
ulong		outchan = CMAP8;
int		defaultcolor = 1;
Image	*image;

enum{
	Border	= 2,
	Edge		= 5
};

char	*show(int, char*);

Rawimage** readtga(int fd);

Rectangle
imager(Image *i)
{
	Point p1, p2;

	p1 = addpt(divpt(subpt(i->r.max, i->r.min), 2), i->r.min);
	p2 = addpt(divpt(subpt(screen->clipr.max, screen->clipr.min), 2), screen->clipr.min);
	return rectaddpt(i->r, subpt(p2, p1));
}

void
eresized(int new)
{
	Rectangle r;

	if(new && getwindow(display, Refnone) < 0){
		fprint(2, "tga: can't reattach to window\n");
		exits("resize");
	}
	if(image == nil)
		return;
	r = imager(image);
	border(screen, r, -Border, nil, ZP);
	drawop(screen, r, image, nil, image->r.min, S);
	flushimage(display, 1);
}

void
main(int argc, char *argv[])
{
	int fd, i;
	char *err;

	ARGBEGIN{
	case '3':		/* produce encoded, compressed, three-color bitmap file; no display by default */
		threeflag++;
		/* fall through */
	case 't':		/* produce encoded, compressed, true-color bitmap file; no display by default */
		cflag++;
		dflag++;
		output++;
		defaultcolor = 0;
		outchan = RGB24;
		break;
	case 'c':		/* produce encoded, compressed, bitmap file; no display by default */
		cflag++;
		dflag++;
		output++;
		if(defaultcolor)
			outchan = CMAP8;
		break;
	case 'd':		/* suppress display of image */
		dflag++;
		break;
	case 'e':		/* disable floyd-steinberg error diffusion */
		eflag++;
		break;
	case 'k':		/* force black and white */
		defaultcolor = 0;
		outchan = GREY8;
		break;
	case 'v':		/* force RGBV */
		defaultcolor = 0;
		outchan = CMAP8;
		break;
	case '9':		/* produce plan 9, uncompressed, bitmap file; no display by default */
		nineflag++;
		dflag++;
		output++;
		if(defaultcolor)
			outchan = CMAP8;
		break;
	default:
		fprint(2, "usage: tga -39cdektv  [file ...]\n");
		exits("usage");
	}ARGEND;

	err = nil;
	if(argc == 0)
		err = show(0, "<stdin>");
	else{
		for(i=0; i<argc; i++){
			fd = open(argv[i], OREAD);
			if(fd < 0){
				fprint(2, "tga: can't open %s: %r\n", argv[i]);
				err = "open";
			}else{
				err = show(fd, argv[i]);
				close(fd);
			}
			if((nineflag || cflag) && argc>1 && err==nil){
				fprint(2, "tga: exiting after one file\n");
				break;
			}
		}
	}
	exits(err);
}

int
init(void)
{
	static int inited;

	if(inited == 0){
		if(initdraw(0, 0, 0) < 0){
			fprint(2, "tga: initdraw failed: %r");
			return -1;
		}
		einit(Ekeyboard|Emouse);
		inited++;
	}
	return 1;
}

char*
show(int fd, char *name)
{
	Rawimage **array, *r, *c;
	Image *i;
	int j, ch;
	char buf[32];

	array = readtga(fd);
	if(array == nil || array[0]==nil){
		fprint(2, "tga: decode %s failed: %r\n", name);
		return "decode";
	}
	if(!dflag){
		if(init() < 0)
			return "initdraw";
		if(defaultcolor && screen->depth>8)
			outchan = RGB24;
	}
	r = array[0];
	if(outchan == CMAP8)
		c = torgbv(r, !eflag);
	else{
		if(outchan==GREY8 || (r->chandesc==CY && threeflag==0)){
			c = totruecolor(r, CY);
			outchan = GREY8;
		}else if(r->chandesc==CRGBA){
			c = totruecolor(r, CRGBA32);
			outchan = RGBA32;
		}else
			c = totruecolor(r, CRGB24);
	}
	if(c == nil){
		fprint(2, "tga: converting %s to local format failed: %r\n", name);
		return "torgbv";
	}
	if(!dflag){
		if(r->chandesc == CY)
			i = allocimage(display, c->r, GREY8, 0, 0);
		else
			i = allocimage(display, c->r, outchan, 0, 0);

		if(i == nil){
			fprint(2, "tga: allocimage %s failed: %r\n", name);
			return "allocimage";
		}

		if(loadimage(i, i->r, c->chans[0], c->chanlen) < 0){
			fprint(2, "tga: loadimage %s failed: %r\n", name);
			return "loadimage";
		}
		image = i;
		eresized(0);
		if((ch=ekbd())=='q' || ch==Kdel || ch==Keof)
			exits(nil);
		draw(screen, screen->clipr, display->white, nil, ZP);
		image = nil;
		freeimage(i);
	}
	if(nineflag){
		chantostr(buf, outchan);
		print("%11s %11d %11d %11d %11d ", buf,
			c->r.min.x, c->r.min.y, c->r.max.x, c->r.max.y);
		if(write(1, c->chans[0], c->chanlen) != c->chanlen){
			fprint(2, "tga: %s: write error %r\n", name);
			return "write";
		}
	}else if(cflag){
		if(writerawimage(1, c) < 0){
			fprint(2, "tga: %s: write error: %r\n", name);
			return "write";
		}
	}
	for(j=0; j<r->nchans; j++)
		free(r->chans[j]);
	free(r->cmap);
	free(r);
	free(array);
	if(c){
		free(c->chans[0]);
		free(c);
	}
	return nil;
}
