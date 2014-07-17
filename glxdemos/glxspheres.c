/* Copyright (C)2007 Sun Microsystems, Inc.
 * Copyright (C)2011, 2013-2014 D. R. Commander
 *
 * This library is free software and may be redistributed and/or modified under
 * the terms of the wxWindows Library License, Version 3.1 or (at your option)
 * any later version.  The full license is in the LICENSE.txt file included
 * with this distribution.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * wxWindows Library License for more details.
 */

/* This program's goal is to reproduce, as closely as possible, the image
   output of the nVidia SphereMark demo by R. Stephen Glanville using the
   simplest available rendering method.  GLXSpheres is meant primarily to
   serve as an image pipeline benchmark for VirtualGL. */


#include <GL/glx.h>
#include <GL/glu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "Timer.h"
#include "vglutil.h"


#define _throw(m) {  \
	fprintf(stderr, "ERROR (%d): %s\n", __LINE__,  m);  \
	goto bailout;  \
}
#define _catch(f) { if((f)==-1) goto bailout; }

#define np2(i) ((i)>0? (1<<(int)(log((double)(i))/log(2.))) : 0)

#define SPHERE_RED(f) fabs(MAXI*(2.*f-1.))
#define SPHERE_GREEN(f) fabs(MAXI*(2.*fmod(f+2./3., 1.)-1.))
#define SPHERE_BLUE(f) fabs(MAXI*(2.*fmod(f+1./3., 1.)-1.))


#define DEF_WIDTH 1240
#define DEF_HEIGHT 900

#define DEF_SLICES 32
#define DEF_STACKS 32

#define NSPHERES 20

#define _2PI 6.283185307180
#define MAXI (220./255.)

#define NSCHEMES 7
enum { GRAY=0, RED, GREEN, BLUE, YELLOW, MAGENTA, CYAN };

#define DEFBENCHTIME 2.0

Display *dpy=NULL;  Window win=0, olwin=0;
int useStereo=0, useOverlay=0, useCI=0, useImm=0, interactive=0, oldb=1,
	loColor=0, maxFrames=0, totalFrames=0, directctx=True;
double benchTime=DEFBENCHTIME;
int nColors=0, nOlColors, colorScheme=GRAY;
Colormap colormap=0, olColormap=0;
GLXContext ctx=0, olctx=0;

int sphereList=0, fontListBase=0;
GLUquadricObj *sphereQuad=NULL;
int slices=DEF_SLICES, stacks=DEF_STACKS;
float x=0., y=0., z=-3.;
float outerAngle=0., middleAngle=0., innerAngle=0.;
float loneSphereColor=0.;
unsigned int transPixel=0;

int width=DEF_WIDTH, height=DEF_HEIGHT;


int setColorScheme(Colormap cmap, int nColors, int scheme)
{
	XColor xc[256];  int i;
	if(!nColors || !cmap) _throw("Color map not allocated");
	if(scheme<0 || scheme>NSCHEMES-1 || !cmap) _throw("Invalid argument");

	for(i=0; i<nColors; i++)
	{
		xc[i].flags=DoRed | DoGreen | DoBlue;
		xc[i].pixel=i;
		xc[i].red=xc[i].green=xc[i].blue=0;
		if(scheme==GRAY || scheme==RED || scheme==YELLOW || scheme==MAGENTA)
			xc[i].red=(i*(256/nColors))<<8;
		if(scheme==GRAY || scheme==GREEN || scheme==YELLOW || scheme==CYAN)
			xc[i].green=(i*(256/nColors))<<8;
		if(scheme==GRAY || scheme==BLUE || scheme==MAGENTA || scheme==CYAN)
			xc[i].blue=(i*(256/nColors))<<8;
	}
	XStoreColors(dpy, cmap, xc, nColors);
	return 0;

	bailout:
	return -1;
}


void reshape(int newWidth, int newHeight)
{
	XWindowChanges changes;
	if(newWidth<=0) newWidth=1;  if(newHeight<=0) newHeight=1;
	width=newWidth;  height=newHeight;
	if(useOverlay && olwin)
	{
		changes.width=width;
		changes.height=height;
		XConfigureWindow(dpy, olwin, CWWidth|CWHeight, &changes);
	}
}


void setSphereColor(float color)
{
	if(useCI)
	{
		GLfloat index=color*(float)(nColors-1);
		GLfloat matIndexes[]={ index*0.3, index*0.8, index };
		glIndexf(index);
		glMaterialfv(GL_FRONT, GL_COLOR_INDEXES, matIndexes);
	}
	else
	{
		float mat[4]=
		{
			SPHERE_RED(color), SPHERE_GREEN(color), SPHERE_BLUE(color), 0.25
		};
		glColor3f(SPHERE_RED(color), SPHERE_GREEN(color), SPHERE_BLUE(color));
		glMaterialfv(GL_FRONT, GL_AMBIENT, mat);
		glMaterialfv(GL_FRONT, GL_DIFFUSE, mat);
	}
}


void renderSpheres(int buf)
{
	int i;
	GLfloat xaspect, yaspect;
	GLfloat stereoCameraOffset=0.;
	GLfloat nearDist=1.5, farDist=40., zeroParallaxDist=17.;

	glDrawBuffer(buf);

	xaspect=(GLfloat)width/(GLfloat)(min(width, height));
	yaspect=(GLfloat)height/(GLfloat)(min(width, height));

	if(buf==GL_BACK_LEFT)
		stereoCameraOffset=-xaspect*zeroParallaxDist/nearDist*0.035;
	else if(buf==GL_BACK_RIGHT)
		stereoCameraOffset=xaspect*zeroParallaxDist/nearDist*0.035;

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glFrustum(-xaspect, xaspect, -yaspect, yaspect, nearDist, farDist);
	glTranslatef(-stereoCameraOffset, 0., 0.);
	glMatrixMode(GL_MODELVIEW);
	glViewport(0, 0, width, height);

	/* Begin rendering */
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClearDepth(20.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* Lone sphere */
	glPushMatrix();
	glTranslatef(0., 0., z);
	setSphereColor(loneSphereColor);
	if(useImm) gluSphere(sphereQuad, 1.3, slices, stacks);
	else glCallList(sphereList);
	glPopMatrix();

	/* Outer ring */
	glPushMatrix();
	glRotatef(outerAngle, 0., 0., 1.);
	for(i=0; i<NSPHERES; i++)
	{
		double f=(double)i/(double)NSPHERES;
		glPushMatrix();
		glTranslatef(sin(_2PI*f)*5., cos(_2PI*f)*5., -10.);
		setSphereColor(f);
		if(useImm) gluSphere(sphereQuad, 1.3, slices, stacks);
		else glCallList(sphereList);
		glPopMatrix();
	}
	glPopMatrix();

	/* Middle ring */
	glPushMatrix();
	glRotatef(middleAngle, 0., 0., 1.);
	for(i=0; i<NSPHERES; i++)
	{
		double f=(double)i/(double)NSPHERES;
		glPushMatrix();
		glTranslatef(sin(_2PI*f)*5., cos(_2PI*f)*5., -17.);
		setSphereColor(f);
		if(useImm) gluSphere(sphereQuad, 1.3, slices, stacks);
		else glCallList(sphereList);
		glPopMatrix();
	}
	glPopMatrix();

	/* Inner ring */
	glPushMatrix();
	glRotatef(innerAngle, 0., 0., 1.);
	for(i=0; i<NSPHERES; i++)
	{
		double f=(double)i/(double)NSPHERES;
		glPushMatrix();
		glTranslatef(sin(_2PI*f)*5., cos(_2PI*f)*5., -29.);
		setSphereColor(f);
		if(useImm) gluSphere(sphereQuad, 1.3, slices, stacks);
		else glCallList(sphereList);
		glPopMatrix();
	}
	glPopMatrix();

	/* Move the eye back to the middle */
	glMatrixMode(GL_PROJECTION);
	glTranslatef(stereoCameraOffset, 0., 0.);
}


void renderOverlay(void)
{
	int i, j, w=width/8, h=height/8;  unsigned char *buf=NULL;
	int index=(int)(loneSphereColor*(float)(nOlColors-1));

	glShadeModel(GL_FLAT);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glViewport(0, 0, width, height);
	glRasterPos3f(-0.5, 0.5, 0.0);
	glClearIndex((float)transPixel);
	glClear(GL_COLOR_BUFFER_BIT);
	glIndexf(loneSphereColor*(float)(nOlColors-1));
	glBegin(GL_LINES);
	glVertex2f(-1.+loneSphereColor*2., -1.);
	glVertex2f(-1.+loneSphereColor*2., 1.);
	glVertex2f(-1., -1.+loneSphereColor*2.);
	glVertex2f(1., -1.+loneSphereColor*2.);
	glEnd();
	if(w && h)
	{
		if((buf=(unsigned char *)malloc(w*h))==NULL)
			_throw("Could not allocate memory");
		for(i=0; i<h; i++)
			for(j=0; j<w; j++)
			{
				if(((i/4)%2)!=((j/4)%2)) buf[i*w+j]=0;
				else buf[i*w+j]=index;
			}
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glDrawPixels(w, h, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, buf);
	}
	glRasterPos3f(0., 0., 0.);
	glListBase(fontListBase);
	glCallLists(24, GL_UNSIGNED_BYTE, "GLX Spheres Overlay Test");

	bailout:
	if(buf) free(buf);
}


int display(int advance)
{
	static int first=1;
	static double start=0., elapsed=0., mpixels=0.;
	static unsigned long frames=0;
	static char temps[256];
	XFontStruct *fontInfo=NULL;  int minChar, maxChar;
	GLfloat xaspect, yaspect;

	if(first)
	{
		GLfloat id4[]={ 1., 1., 1., 1. };
		GLfloat light0Amb[]={ 0.3, 0.3, 0.3, 1. };
		GLfloat light0Dif[]={ 0.8, 0.8, 0.8, 1. };
		GLfloat light0Pos[]={ 1., 1., 1., 0. };

		sphereList=glGenLists(1);
		if(!(sphereQuad=gluNewQuadric()))
			_throw("Could not allocate GLU quadric object");
		glNewList(sphereList, GL_COMPILE);
		gluSphere(sphereQuad, 1.3, slices, stacks);
		glEndList();

		if(!loColor)
		{
			if(useCI)
			{
				glMaterialf(GL_FRONT, GL_SHININESS, 50.);
			}
			else
			{
				glMaterialfv(GL_FRONT, GL_AMBIENT, id4);
				glMaterialfv(GL_FRONT, GL_DIFFUSE, id4);
				glMaterialfv(GL_FRONT, GL_SPECULAR, id4);
				glMaterialf(GL_FRONT, GL_SHININESS, 50.);

				glLightfv(GL_LIGHT0, GL_AMBIENT, light0Amb);
				glLightfv(GL_LIGHT0, GL_DIFFUSE, light0Dif);
				glLightfv(GL_LIGHT0, GL_SPECULAR, id4);
			}
			glLightfv(GL_LIGHT0, GL_POSITION, light0Pos);
			glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, 180.);
			glEnable(GL_LIGHTING);
			glEnable(GL_LIGHT0);
		}

		if(loColor) glShadeModel(GL_FLAT);
		else glShadeModel(GL_SMOOTH);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);

		if(useOverlay)
		{
			glXMakeCurrent(dpy, olwin, olctx);
		}
		if(!(fontInfo=XLoadQueryFont(dpy, "fixed")))
			_throw("Could not load X font");
		minChar=fontInfo->min_char_or_byte2;
		maxChar=fontInfo->max_char_or_byte2;
		fontListBase=glGenLists(maxChar+1);
		glXUseXFont(fontInfo->fid, minChar, maxChar-minChar+1,
			fontListBase+minChar);
		XFreeFont(dpy, fontInfo);  fontInfo=NULL;
		snprintf(temps, 255, "Measuring performance ...");
		if(useOverlay) glXMakeCurrent(dpy, win, ctx);

		first=0;
	}

	if(advance)
	{
		z-=0.5;
		if(z<-29.)
		{
			if(useCI || useOverlay) colorScheme=(colorScheme+1)%NSCHEMES;
			if(useCI) _catch(setColorScheme(colormap, nColors, colorScheme));
			if(useOverlay)
				_catch(setColorScheme(olColormap, nOlColors, colorScheme));
			z=-3.5;
		}
		outerAngle+=0.1;  if(outerAngle>360.) outerAngle-=360.;
		middleAngle-=0.37;  if(middleAngle<-360.) middleAngle+=360.;
		innerAngle+=0.63;  if(innerAngle>360.) innerAngle-=360.;
		loneSphereColor+=0.005;
		if(loneSphereColor>1.) loneSphereColor-=1.;
	}

	if(useStereo)
	{
		renderSpheres(GL_BACK_LEFT);  renderSpheres(GL_BACK_RIGHT);
	}
	else renderSpheres(GL_BACK);

	glDrawBuffer(GL_BACK);
	if(useOverlay)
	{
		glXMakeCurrent(dpy, olwin, olctx);
		renderOverlay();
	}
	else
	glPushAttrib(GL_CURRENT_BIT);
	glPushAttrib(GL_LIST_BIT);
	glPushAttrib(GL_ENABLE_BIT);
	glDisable(GL_LIGHTING);
	if(useOverlay || useCI) glIndexf(254.);
	else glColor3f(1., 1., 1.);
	if(useOverlay) glRasterPos3f(-0.95, -0.95, 0.);
	else
	{
		xaspect=(GLfloat)width/(GLfloat)(min(width, height));
		yaspect=(GLfloat)height/(GLfloat)(min(width, height));
		glRasterPos3f(-0.95*xaspect, -0.95*yaspect, -1.5);
	}
	glListBase(fontListBase);
	glCallLists(strlen(temps), GL_UNSIGNED_BYTE, temps);
	glPopAttrib();
	glPopAttrib();
	glPopAttrib();
	if(useOverlay)
	{
		if(oldb) glXSwapBuffers(dpy, olwin);
		glXMakeCurrent(dpy, win, ctx);
	}
	glXSwapBuffers(dpy, win);

	if(start>0.)
	{
		elapsed+=getTime()-start;  frames++;  totalFrames++;
		mpixels+=(double)width*(double)height/1000000.;
		if(elapsed>benchTime || (maxFrames && totalFrames>maxFrames))
		{
			snprintf(temps, 255, "%f frames/sec - %f Mpixels/sec",
				(double)frames/elapsed, mpixels/elapsed);
			printf("%s\n", temps);
			elapsed=mpixels=0.;  frames=0;
		}
	}
	if(maxFrames && totalFrames>maxFrames) goto bailout;

	start=getTime();
	return 0;

	bailout:
	if(sphereQuad) { gluDeleteQuadric(sphereQuad);  sphereQuad=NULL; }
	if(fontInfo) { XFreeFont(dpy, fontInfo);  fontInfo=NULL; }
	return -1;
}


Atom protoAtom=0, deleteAtom=0;


int eventLoop(Display *dpy)
{
	while (1)
	{
		int advance=0, doDisplay=0;

		while(1)
		{
			XEvent event;
			if(interactive) XNextEvent(dpy, &event);
			else
			{
				if(XPending(dpy)>0) XNextEvent(dpy, &event);
				else break;
			}
			switch (event.type)
			{
				case Expose:
					doDisplay=1;
					break;
				case ConfigureNotify:
					reshape(event.xconfigure.width, event.xconfigure.height);
					break;
				case KeyPress:
				{
					char buf[10];
					XLookupString(&event.xkey, buf, sizeof(buf), NULL, NULL);
					switch(buf[0])
					{
						case 27: case 'q': case 'Q':
							return 0;
					}
					break;
				}
				case MotionNotify:
					if(event.xmotion.state & Button1Mask) doDisplay=advance=1;
 					break;
				case ClientMessage:
				{
					XClientMessageEvent *cme=(XClientMessageEvent *)&event;
					if(cme->message_type==protoAtom && cme->data.l[0]==deleteAtom)
						return 0;
				}
			}
			if(interactive)
			{
				if(XPending(dpy)<=0) break;
			}
		}
		if(!interactive) { _catch(display(1)); }
		else { if(doDisplay) { _catch(display(advance)); }}
	}

	bailout:
	return -1;
}


void usage(char **argv)
{
	printf("\nUSAGE: %s [options]\n\n", argv[0]);
	printf("Options:\n");
	printf("-c = Use color index rendering (default is RGB)\n");
	printf("-fs = Full-screen mode\n");
	printf("-i = Interactive mode.  Frames advance in response to mouse movement\n");
	printf("-l = Use fewer than 24 colors (to force non-JPEG encoding in TurboVNC)\n");
	printf("-m = Use immediate mode rendering (default is display list)\n");
	printf("-o = Test 8-bit transparent overlays\n");
	printf("-p <p> = Use (approximately) <p> polygons to render scene\n");
	printf("-s = Use stereographic rendering initially\n");
	printf("     (this can be switched on and off in the application)\n");
	printf("-32 = Use 32-bit visual (default is 24-bit)\n");
	printf("-f <n> = max frames to render\n");
	printf("-bt <t> = print benchmark results every <t> seconds (default=%.1f)\n",
		DEFBENCHTIME);
	printf("-w <wxh> = specify window width and height\n");
	printf("-ic = Use indirect rendering context\n");
	printf("-sc <s> = Create window on X screen # <s>\n");
	printf("\n");
	exit(0);
}


int main(int argc, char **argv)
{
	int i, usealpha=0;
	XVisualInfo *v=NULL;
	int rgbattribs[]={ GLX_RGBA, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
		GLX_BLUE_SIZE, 8, GLX_DEPTH_SIZE, 1, GLX_DOUBLEBUFFER, None, None, None,
		None };
	int ciattribs[]={ GLX_BUFFER_SIZE, 8, GLX_DEPTH_SIZE, 1, GLX_DOUBLEBUFFER,
		None };
	int olattribs[]={ GLX_BUFFER_SIZE, 8, GLX_LEVEL, 1, GLX_TRANSPARENT_TYPE,
		GLX_TRANSPARENT_INDEX, GLX_DOUBLEBUFFER, None };
	XSetWindowAttributes swa;  Window root;
	int fullScreen=0;  unsigned long mask=0;
	int screen=-1;

	for(i=0; i<argc; i++)
	{
		if(!strnicmp(argv[i], "-h", 2)) usage(argv);
		if(!strnicmp(argv[i], "-?", 2)) usage(argv);
		if(!strnicmp(argv[i], "-c", 2)) useCI=1;
		if(!strnicmp(argv[i], "-ic", 3)) directctx=False;
		else if(!strnicmp(argv[i], "-i", 2)) interactive=1;
		if(!strnicmp(argv[i], "-l", 2)) loColor=1;
		if(!strnicmp(argv[i], "-m", 2)) useImm=1;
		if(!strnicmp(argv[i], "-o", 2)) useOverlay=1;
		if(!strnicmp(argv[i], "-w", 2) && i<argc-1)
		{
			int w=0, h=0;
			if(sscanf(argv[++i], "%dx%d", &w, &h)==2 && w>0 && h>0)
			{
				width=w;  height=h;
				printf("Window dimensions: %d x %d\n", width, height);
			}
		}
		if(!strnicmp(argv[i], "-p", 2) && i<argc-1)
		{
			int nPolys=atoi(argv[++i]);
			if(nPolys>0)
			{
				slices=stacks=(int)(sqrt((double)nPolys/((double)(3*NSPHERES+1))));
				if(slices<1) slices=stacks=1;
			}
		}
		if(!strnicmp(argv[i], "-fs", 3)) fullScreen=1;
		else if(!strnicmp(argv[i], "-f", 2) && i<argc-1)
		{
			int mf=atoi(argv[++i]);
			if(mf>0)
			{
				maxFrames=mf;
				printf("Number of frames to render: %d\n", maxFrames);
			}
		}
		if(!strnicmp(argv[i], "-bt", 3) && i<argc-1)
		{
			double temp=atof(argv[++i]);
			if(temp>0.0) benchTime=temp;
		}
		if(!strnicmp(argv[i], "-sc", 3) && i<argc-1)
		{
			int sc=atoi(argv[++i]);
			if(sc>0)
			{
				screen=sc;
				printf("Rendering to screen %d\n", screen);
			}
		}
		else if(!strnicmp(argv[i], "-s", 2))
		{
			rgbattribs[10]=GLX_STEREO;
			useStereo=1;
		}
		if(!strnicmp(argv[i], "-32", 3)) usealpha=1;
	}

	if(usealpha)
	{
		if(rgbattribs[10]==None)
		{
			rgbattribs[10]=GLX_ALPHA_SIZE;
			rgbattribs[11]=8;
		}
		else
		{
			rgbattribs[11]=GLX_ALPHA_SIZE;
			rgbattribs[12]=8;
		}
	}

	fprintf(stderr, "Polygons in scene: %d\n", (NSPHERES*3+1)*slices*stacks);

	if((dpy=XOpenDisplay(0))==NULL) _throw("Could not open display");
	if(screen<0) screen=DefaultScreen(dpy);

	if(useCI)
	{
		if((v=glXChooseVisual(dpy, screen, ciattribs))==NULL)
			_throw("Could not obtain index visual");
	}
	else
	{
		if((v=glXChooseVisual(dpy, screen, rgbattribs))==NULL)
			_throw("Could not obtain RGB visual with requested properties");
	}
	fprintf(stderr, "Visual ID of %s: 0x%.2x\n", useOverlay? "underlay":"window",
		(int)v->visualid);

	root=RootWindow(dpy, screen);
	swa.border_pixel=0;
	swa.event_mask=StructureNotifyMask|ExposureMask|KeyPressMask;
	swa.override_redirect=fullScreen? True:False;
	if(useCI)
	{
		swa.colormap=colormap=XCreateColormap(dpy, root, v->visual, AllocAll);
		nColors=np2(v->colormap_size);
		if(nColors<32) _throw("Color map is not large enough");
		_catch(setColorScheme(colormap, nColors, colorScheme));
	}
	else swa.colormap=XCreateColormap(dpy, root, v->visual, AllocNone);

	if(interactive) swa.event_mask|=PointerMotionMask|ButtonPressMask;

	mask=CWBorderPixel|CWColormap|CWEventMask;
	if(fullScreen)
	{
		mask|=CWOverrideRedirect;
		width=DisplayWidth(dpy, screen);
		height=DisplayHeight(dpy, screen);
	}
	if(!(protoAtom=XInternAtom(dpy, "WM_PROTOCOLS", False)))
		_throw("Cannot obtain WM_PROTOCOLS atom");
	if(!(deleteAtom=XInternAtom(dpy, "WM_DELETE_WINDOW", False)))
		_throw("Cannot obtain WM_DELETE_WINDOW atom");
	if((win=XCreateWindow(dpy, root, 0, 0, width, height, 0, v->depth,
		InputOutput, v->visual, mask, &swa))==0)
		_throw("Could not create window");
	XSetWMProtocols(dpy, win, &deleteAtom, 1);
	XStoreName(dpy, win, "GLX Spheres");
	XMapWindow(dpy, win);
	if(fullScreen)
	{
		XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
		XGrabKeyboard(dpy, win, GrabModeAsync, True, GrabModeAsync, CurrentTime);
	}
	XSync(dpy, False);

	if((ctx=glXCreateContext(dpy, v, NULL, directctx))==0)
		_throw("Could not create rendering context");
	fprintf(stderr, "Context is %s\n",
		glXIsDirect(dpy, ctx)? "Direct":"Indirect");
	XFree(v);  v=NULL;

	if(useOverlay)
	{
		if((v=glXChooseVisual(dpy, screen, olattribs))==NULL)
		{
			olattribs[6]=None;  oldb=0;
			if((v=glXChooseVisual(dpy, screen, olattribs))==NULL)
				_throw("Could not obtain overlay visual");
		}
		fprintf(stderr, "Visual ID of overlay: 0x%.2x\n",	(int)v->visualid);

		swa.colormap=olColormap=XCreateColormap(dpy, root, v->visual, AllocAll);
		nOlColors=np2(v->colormap_size);
		if(nOlColors<32) _throw("Color map is not large enough");

		_catch(setColorScheme(olColormap, nOlColors, colorScheme));

		if((olwin=XCreateWindow(dpy, win, 0, 0, width, height, 0, v->depth,
			InputOutput, v->visual, CWBorderPixel|CWColormap|CWEventMask, &swa))==0)
			_throw("Could not create overlay window");
		XMapWindow(dpy, olwin);
		XSync(dpy, False);

		if((olctx=glXCreateContext(dpy, v, NULL, directctx))==0)
			_throw("Could not create overlay rendering context");
		fprintf(stderr, "Overlay context is %s\n",
			glXIsDirect(dpy, olctx)? "Direct":"Indirect");

		XFree(v);  v=NULL;
	}

	if(!glXMakeCurrent(dpy, win, ctx))
		_throw("Could not bind rendering context");

	fprintf(stderr, "OpenGL Renderer: %s\n", glGetString(GL_RENDERER));

	_catch(eventLoop(dpy));

	if(dpy && olctx) { glXDestroyContext(dpy, olctx);  olctx=0; }
	if(dpy && olwin) { XDestroyWindow(dpy, olwin);  olwin=0; }
	if(dpy && ctx) { glXDestroyContext(dpy, ctx);  ctx=0; }
	if(dpy && win) { XDestroyWindow(dpy, win);  win=0; }
	if(dpy) XCloseDisplay(dpy);
	return 0;

	bailout:
	if(v) XFree(v);
	if(dpy && olctx) { glXDestroyContext(dpy, olctx);  olctx=0; }
	if(dpy && olwin) { XDestroyWindow(dpy, olwin);  olwin=0; }
	if(dpy && ctx) { glXDestroyContext(dpy, ctx);  ctx=0; }
	if(dpy && win) { XDestroyWindow(dpy, win);  win=0; }
	if(dpy) XCloseDisplay(dpy);
	return -1;
}
