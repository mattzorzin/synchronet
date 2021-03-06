/* js_file.c */

/* Synchronet JavaScript "File" Object */

/* $Id: js_file.c,v 1.157 2014/01/06 06:09:19 rswindell Exp $ */

/****************************************************************************
 * @format.tab-size 4		(Plain Text/Source Code File Header)			*
 * @format.use-tabs true	(see http://www.synchro.net/ptsc_hdr.html)		*
 *																			*
 * Copyright 2013 Rob Swindell - http://www.synchro.net/copyright.html		*
 *																			*
 * This program is free software; you can redistribute it and/or			*
 * modify it under the terms of the GNU General Public License				*
 * as published by the Free Software Foundation; either version 2			*
 * of the License, or (at your option) any later version.					*
 * See the GNU General Public License for more details: gpl.txt or			*
 * http://www.fsf.org/copyleft/gpl.html										*
 *																			*
 * Anonymous FTP access to the most recent released source is available at	*
 * ftp://vert.synchro.net, ftp://cvs.synchro.net and ftp://ftp.synchro.net	*
 *																			*
 * Anonymous CVS access to the development source and modification history	*
 * is available at cvs.synchro.net:/cvsroot/sbbs, example:					*
 * cvs -d :pserver:anonymous@cvs.synchro.net:/cvsroot/sbbs login			*
 *     (just hit return, no password is necessary)							*
 * cvs -d :pserver:anonymous@cvs.synchro.net:/cvsroot/sbbs checkout src		*
 *																			*
 * For Synchronet coding style and modification guidelines, see				*
 * http://www.synchro.net/source.html										*
 *																			*
 * You are encouraged to submit any modifications (preferably in Unix diff	*
 * format) via e-mail to mods@synchro.net									*
 *																			*
 * Note: If this box doesn't appear square, then you need to fix your tabs.	*
 ****************************************************************************/

#include "sbbs.h"
#include "md5.h"
#include "base64.h"
#include "uucode.h"
#include "yenc.h"
#include "ini_file.h"

#ifdef JAVASCRIPT

#include "js_request.h"

typedef struct
{
	FILE*	fp;
	char	name[MAX_PATH+1];
	char	mode[4];
	uchar	etx;
	BOOL	external;	/* externally created, don't close */
	BOOL	debug;
	BOOL	rot13;
	BOOL	yencoded;
	BOOL	uuencoded;
	BOOL	b64encoded;
	BOOL	network_byte_order;
	BOOL	pipe;		/* Opened with popen() use pclose() to close */

} private_t;

static const char* getprivate_failure = "line %d %s JS_GetPrivate failed";

static void dbprintf(BOOL error, private_t* p, char* fmt, ...)
{
	va_list argptr;
	char sbuf[1024];

	if(p==NULL || (!p->debug && !error))
		return;

    va_start(argptr,fmt);
    vsnprintf(sbuf,sizeof(sbuf),fmt,argptr);
	sbuf[sizeof(sbuf)-1]=0;
    va_end(argptr);
	
	lprintf(LOG_DEBUG,"%04u File %s%s",p->fp ? fileno(p->fp) : 0,error ? "ERROR: ":"",sbuf);
}

/* Converts fopen() style 'mode' string into open() style 'flags' integer */

static int fopenflags(char *mode)
{
	int flags=0;

	if(strchr(mode,'b'))
		flags|=O_BINARY;
	else
		flags|=O_TEXT;

	if(strchr(mode,'w')) {
		flags|=O_CREAT|O_TRUNC;
		if(strchr(mode,'+'))
			flags|=O_RDWR;
		else
			flags|=O_WRONLY;
		return(flags);
	}

	if(strchr(mode,'a')) {
		flags|=O_CREAT|O_APPEND;
		if(strchr(mode,'+'))
			flags|=O_RDWR;
		else
			flags|=O_WRONLY;
		return(flags);
	}

	if(strchr(mode,'r')) {
		if(strchr(mode,'+'))
			flags|=O_RDWR;
		else
			flags|=O_RDONLY;
	}

	if(strchr(mode,'e'))
		flags|=O_EXCL;

	return(flags);
}

/* File Object Methods */

static JSBool
js_open(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	BOOL		shareable=FALSE;
	int			file;
	uintN		i;
	jsint		bufsize=2*1024;
	JSString*	str;
	private_t*	p;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_FALSE);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp!=NULL)  
		return(JS_TRUE);

	SAFECOPY(p->mode,"w+");		/* default mode */
	for(i=0;i<argc;i++) {
		if(JSVAL_IS_STRING(argv[i])) {	/* mode */
			if((str = JS_ValueToString(cx, argv[i]))==NULL) {
				JS_ReportError(cx,"Invalid mode specified: %s",str);
				return(JS_TRUE);
			}
			JSSTRING_TO_STRBUF(cx, str, p->mode, sizeof(p->mode), NULL);
		}
		else if(JSVAL_IS_BOOLEAN(argv[i]))	/* shareable */
			shareable=JSVAL_TO_BOOLEAN(argv[i]);
		else if(JSVAL_IS_NUMBER(argv[i])) {	/* bufsize */
			if(!JS_ValueToInt32(cx,argv[i],&bufsize))
				return(JS_FALSE);
		}
	}

	rc=JS_SUSPENDREQUEST(cx);
	if(shareable)
		p->fp=fopen(p->name,p->mode);
	else {
		if((file=nopen(p->name,fopenflags(p->mode)))!=-1) {
			char *fdomode=strdup(p->mode);
			char *e=fdomode;

			if(fdomode && e) {
				for(e=strchr(fdomode, 'e'); e ; e=strchr(e, 'e'))
					memmove(e, e+1, strlen(e));
				if((p->fp=fdopen(file,fdomode))==NULL)
					close(file);
			}
			free(fdomode);
		}
	}
	if(p->fp!=NULL) {
		JS_SET_RVAL(cx, arglist, JSVAL_TRUE);
		dbprintf(FALSE, p, "opened: %s",p->name);
		if(!bufsize)
			setvbuf(p->fp,NULL,_IONBF,0);	/* no buffering */
		else {
#ifdef _WIN32
			if(bufsize < 2)
				bufsize = 2;
#endif
			setvbuf(p->fp,NULL,_IOFBF,bufsize);
		}
	}
	JS_RESUMEREQUEST(cx, rc);

	return(JS_TRUE);
}

static JSBool
js_popen(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	uintN		i;
	jsint		bufsize=2*1024;
	JSString*	str;
	private_t*	p;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_FALSE);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp!=NULL)  
		return(JS_TRUE);

	SAFECOPY(p->mode,"r+");	/* default mode */
	for(i=0;i<argc;i++) {
		if(JSVAL_IS_STRING(argv[i])) {	/* mode */
			if((str = JS_ValueToString(cx, argv[i]))==NULL) {
				JS_ReportError(cx,"Invalid mode specified: %s",str);
				return(JS_TRUE);
			}
			JSSTRING_TO_STRBUF(cx, str, p->mode, sizeof(p->mode), NULL);
		}
		else if(JSVAL_IS_NUMBER(argv[i])) {	/* bufsize */
			if(!JS_ValueToInt32(cx,argv[i],&bufsize))
				return(JS_FALSE);
		}
	}

	rc=JS_SUSPENDREQUEST(cx);
	p->fp=popen(p->name,p->mode);
	if(p->fp!=NULL) {
		p->pipe=TRUE;
		JS_SET_RVAL(cx, arglist, JSVAL_TRUE);
		dbprintf(FALSE, p, "popened: %s",p->name);
		if(!bufsize)
			setvbuf(p->fp,NULL,_IONBF,0);	/* no buffering */
		else
			setvbuf(p->fp,NULL,_IOFBF,bufsize);
	}
	JS_RESUMEREQUEST(cx, rc);

	return(JS_TRUE);
}

static JSBool
js_close(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	private_t*	p;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_VOID);
	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)
		return(JS_TRUE);

	rc=JS_SUSPENDREQUEST(cx);
#ifdef __unix__
	if(p->pipe)
		pclose(p->fp);
	else
#endif
		fclose(p->fp);

	dbprintf(FALSE, p, "closed");

	p->fp=NULL; 
	JS_RESUMEREQUEST(cx, rc);

	return(JS_TRUE);
}

static JSBool
js_read(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	char*		cp;
	char*		buf;
	char*		uubuf;
	int32		len;
	int32		offset;
	int32		uulen;
	JSString*	str;
	private_t*	p;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_NULL);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)
		return(JS_TRUE);

	if(argc) {
		if(!JS_ValueToInt32(cx,argv[0],&len))
			return(JS_FALSE);
	} else {
		rc=JS_SUSPENDREQUEST(cx);
		len=(long)filelength(fileno(p->fp));
		offset=(long)ftell(p->fp);
		if(offset>0)
			len-=offset;
		JS_RESUMEREQUEST(cx, rc);
	}
	if(len<0)
		len=512;

	if((buf=malloc(len+1))==NULL)
		return(JS_TRUE);

	rc=JS_SUSPENDREQUEST(cx);
	len = fread(buf,1,len,p->fp);
	if(len<0) 
		len=0;
	buf[len]=0;

	if(p->etx) {
		cp=strchr(buf,p->etx);
		if(cp) *cp=0; 
		len=strlen(buf);
	}

	if(p->rot13)
		rot13(buf);

	if(p->uuencoded || p->b64encoded || p->yencoded) {
		uulen=len*2;
		if((uubuf=malloc(uulen))==NULL) {
			free(buf);
			JS_RESUMEREQUEST(cx, rc);
			return(JS_TRUE);
		}
		if(p->uuencoded)
			uulen=uuencode(uubuf,uulen,buf,len);
		else if(p->yencoded)
			uulen=yencode(uubuf,uulen,buf,len);
		else
			uulen=b64_encode(uubuf,uulen,buf,len);
		if(uulen>=0) {
			free(buf);
			buf=uubuf;
			len=uulen;
		}
		else
			free(uubuf);
	}
	JS_RESUMEREQUEST(cx, rc);

	str = JS_NewStringCopyN(cx, buf, len);
	free(buf);

	if(str==NULL)
		return(JS_FALSE);

	JS_SET_RVAL(cx, arglist, STRING_TO_JSVAL(str));

	rc=JS_SUSPENDREQUEST(cx);
	dbprintf(FALSE, p, "read %u bytes",len);
	JS_RESUMEREQUEST(cx, rc);
		
	return(JS_TRUE);
}

static JSBool
js_readln(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	char*		cp;
	char*		buf;
	int32		len=512;
	JSString*	js_str;
	private_t*	p;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_NULL);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)
		return(JS_TRUE);
	
	if(argc) {
		if(!JS_ValueToInt32(cx,argv[0],&len))
			return(JS_FALSE);
	}

	if((buf=malloc(len))==NULL)
		return(JS_FALSE);

	rc=JS_SUSPENDREQUEST(cx);
	if(fgets(buf,len,p->fp)!=NULL) {
		len=strlen(buf);
		while(len>0 && (buf[len-1]=='\r' || buf[len-1]=='\n'))
			len--;
		buf[len]=0;
		if(p->etx) {
			cp=strchr(buf,p->etx);
			if(cp) *cp=0; 
		}
		if(p->rot13)
			rot13(buf);
		JS_RESUMEREQUEST(cx, rc);
		if((js_str=JS_NewStringCopyZ(cx,buf))!=NULL)	/* exception here Feb-12-2005 */
			JS_SET_RVAL(cx, arglist, STRING_TO_JSVAL(js_str));
	} else {
		JS_RESUMEREQUEST(cx, rc);
	}
	free(buf);

	return(JS_TRUE);
}

static JSBool
js_readbin(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	BYTE		*b;
	WORD		*w;
	DWORD		*l;
	int32		size=sizeof(DWORD);
	private_t*	p;
	int32		count=1;
	size_t		retlen;
	void		*buffer=NULL;
	size_t		i;
    JSObject*	array;
    jsval       v;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, INT_TO_JSVAL(-1));

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)
		return(JS_TRUE);

	if(argc) {
		if(!JS_ValueToInt32(cx,argv[0],&size))
			return(JS_FALSE);
		if(argc>1) {
			if(!JS_ValueToInt32(cx,argv[1],&count))
				return(JS_FALSE);
		}
	}

	rc=JS_SUSPENDREQUEST(cx);
	if(size != sizeof(BYTE) && size != sizeof(WORD) && size != sizeof(DWORD)) {
		/* unknown size */
		dbprintf(TRUE, p, "unsupported binary read size: %d",size);
		JS_RESUMEREQUEST(cx, rc);
		return(JS_TRUE);
	}

	buffer=malloc(size*count);
	if(buffer==NULL) {
		dbprintf(TRUE, p, "malloc failure of %u bytes", size*count);
		JS_RESUMEREQUEST(cx, rc);
		return(JS_FALSE);
	}
	b=buffer;
	w=buffer;
	l=buffer;
	retlen=fread(buffer, size, count, p->fp);
	if(count==1) {
		if(retlen==1) {
			switch(size) {
				case sizeof(BYTE):
					JS_SET_RVAL(cx, arglist, INT_TO_JSVAL(*b));
					break;
				case sizeof(WORD):
					JS_SET_RVAL(cx, arglist, INT_TO_JSVAL(*w));
					break;
				case sizeof(DWORD):
					JS_SET_RVAL(cx, arglist, UINT_TO_JSVAL(*l));
					break;
			}
		}
	}
	else {
		JS_RESUMEREQUEST(cx, rc);
    	array = JS_NewArrayObject(cx, 0, NULL);

		for(i=0; i<retlen; i++) {
			switch(size) {
				case sizeof(BYTE):
					v = INT_TO_JSVAL(*(b++));
					break;
				case sizeof(WORD):
					v = INT_TO_JSVAL(*(w++));
					break;
				case sizeof(DWORD):
					v=UINT_TO_JSVAL(*(l++));
					break;
			}
        	if(!JS_SetElement(cx, array, i, &v)) {
				rc=JS_SUSPENDREQUEST(cx);
				goto end;
			}
		}
    	JS_SET_RVAL(cx, arglist, OBJECT_TO_JSVAL(array));
	}

end:
	free(buffer);
	JS_RESUMEREQUEST(cx, rc);
	return(JS_TRUE);
}

static JSBool
js_readall(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
    jsint       len=0;
    JSObject*	array;
	private_t*	p;

	JS_SET_RVAL(cx, arglist, JSVAL_NULL);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)
		return(JS_TRUE);

    array = JS_NewArrayObject(cx, 0, NULL);

    while(!feof(p->fp)) {
		js_readln(cx, argc, arglist);
		if(JS_RVAL(cx, arglist)==JSVAL_NULL)
			break;
        if(!JS_SetElement(cx, array, len++, &JS_RVAL(cx, arglist)))
			break;
	}
    JS_SET_RVAL(cx, arglist, OBJECT_TO_JSVAL(array));

    return(JS_TRUE);
}

static jsval get_value(JSContext *cx, char* value)
{
	char*	p;
	BOOL	f=FALSE;
	jsval	val;

	if(value==NULL || *value==0)
		return(JSVAL_VOID);

	/* integer or float? */
	for(p=value;*p;p++) {
		if(*p=='.' && !f)
			f=TRUE;
		else if(!isdigit((uchar)*p))
			break;
	}
	if(*p==0) {
		if(f)
			val=DOUBLE_TO_JSVAL(atof(value));
		else
			val=DOUBLE_TO_JSVAL((double)strtoul(value,NULL,10));
		return(val);
	}
	/* hexadecimal number? */
	if(!strncmp(value,"0x",2)) {	
		for(p=value+2;*p;p++)
			if(!isxdigit((uchar)*p))
				break;
		if(*p==0) {	
			val=DOUBLE_TO_JSVAL((double)strtoul(value,NULL,0));
			return(val);
		}
	}
	/* Boolean? */
	if(!stricmp(value,"true"))
		return(JSVAL_TRUE);
	if(!stricmp(value,"false"))
		return(JSVAL_FALSE);

	/* String */
	return(STRING_TO_JSVAL(JS_NewStringCopyZ(cx,value)));
}

static double js_DateGetMsecSinceEpoch(JSContext *cx, JSObject *obj)
{
	jsval	rval;

	if(!JS_CallFunctionName(cx, obj, "getTime", 0, NULL, &rval)) {
		return ((double)time(NULL))*1000;
	}
	return JSVAL_TO_DOUBLE(rval);
}

static JSBool
js_iniGetValue(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	char*	section=ROOT_SECTION;
	char*	key;
	char**	list;
	char	buf[INI_MAX_VALUE_LEN];
	int32	i;
	jsval	val;
	jsval	dflt=argv[2];
	private_t*	p;
	JSObject*	array;
	JSObject*	dflt_obj;
	JSObject*	date_obj;
	jsrefcount	rc;
	double		dbl;
	time_t		tt;
	char*		cstr=NULL;
	char*		cstr2;

	JS_SET_RVAL(cx, arglist, JSVAL_VOID);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(argc && argv[0]!=JSVAL_VOID && argv[0]!=JSVAL_NULL)
		JSVALUE_TO_MSTRING(cx, argv[0], section, NULL);
	JSVALUE_TO_MSTRING(cx, argv[1], key, NULL);
	if(JS_IsExceptionPending(cx)) {
		FREE_AND_NULL(section);
		FREE_AND_NULL(key);
		return JS_FALSE;
	}
	/*
	 * Although section can be NULL (ie: root), a NULL key will cause a
	 * segfault.
	 */
	if(key==NULL) {
		JS_ReportError(cx, "Invalid NULL key specified");
		FREE_AND_NULL(section);
		return JS_FALSE;
	}

	if(argc < 3 || dflt==JSVAL_VOID) {	/* unspecified default value */
		rc=JS_SUSPENDREQUEST(cx);
		cstr=iniReadString(p->fp,section,key,NULL,buf);
		FREE_AND_NULL(section);
		FREE_AND_NULL(key);
		JS_RESUMEREQUEST(cx, rc);
		JS_SET_RVAL(cx, arglist, get_value(cx, cstr));
		return(JS_TRUE);
	}

	if(JSVAL_IS_BOOLEAN(dflt)) {
		JS_SET_RVAL(cx,arglist,BOOLEAN_TO_JSVAL(
			iniReadBool(p->fp,section,key,JSVAL_TO_BOOLEAN(dflt))));
	}
	else if(JSVAL_IS_OBJECT(dflt)) {
		if((dflt_obj = JSVAL_TO_OBJECT(dflt))!=NULL && (strcmp("Date",JS_GetClass(cx, dflt_obj)->name)==0)) {
			tt=(time_t)(js_DateGetMsecSinceEpoch(cx,dflt_obj)/1000.0);
			rc=JS_SUSPENDREQUEST(cx);
			dbl=(double)iniReadDateTime(p->fp,section,key,tt);
			dbl *= 1000;
			JS_RESUMEREQUEST(cx, rc);
			date_obj = JS_NewDateObjectMsec(cx, dbl);
			if(date_obj!=NULL) {
				JS_SET_RVAL(cx, arglist, OBJECT_TO_JSVAL(date_obj));
			}
		}
		else {
		    array = JS_NewArrayObject(cx, 0, NULL);
			cstr=NULL;
			JSVALUE_TO_MSTRING(cx, dflt, cstr, NULL);
			if(JS_IsExceptionPending(cx)) {
				FREE_AND_NULL(cstr);
				FREE_AND_NULL(section);
				FREE_AND_NULL(key);
				return JS_FALSE;
			}
			rc=JS_SUSPENDREQUEST(cx);
			list=iniReadStringList(p->fp,section,key,",",cstr);
			FREE_AND_NULL(cstr);
			JS_RESUMEREQUEST(cx, rc);
			for(i=0;list && list[i];i++) {
				val=STRING_TO_JSVAL(JS_NewStringCopyZ(cx,list[i]));
				if(!JS_SetElement(cx, array, i, &val))
					break;
			}
			rc=JS_SUSPENDREQUEST(cx);
			iniFreeStringList(list);
			JS_RESUMEREQUEST(cx, rc);
			JS_SET_RVAL(cx, arglist, OBJECT_TO_JSVAL(array));
		}
	}
	else if(JSVAL_IS_DOUBLE(dflt)) {
		rc=JS_SUSPENDREQUEST(cx);
		dbl=iniReadFloat(p->fp,section,key,JSVAL_TO_DOUBLE(dflt));
		JS_RESUMEREQUEST(cx, rc);
		JS_SET_RVAL(cx, arglist,DOUBLE_TO_JSVAL(dbl));
	}
	else if(JSVAL_IS_NUMBER(dflt)) {
		if(!JS_ValueToInt32(cx,dflt,&i)) {
			FREE_AND_NULL(section);
			FREE_AND_NULL(key);
			return(JS_FALSE);
		}
		rc=JS_SUSPENDREQUEST(cx);
		i=iniReadInteger(p->fp,section,key,i);
		JS_RESUMEREQUEST(cx, rc);
		JS_SET_RVAL(cx, arglist,INT_TO_JSVAL(i));
	} else {
		cstr=NULL;
		JSVALUE_TO_MSTRING(cx, dflt, cstr, NULL);
		if(JS_IsExceptionPending(cx)) {
			FREE_AND_NULL(cstr);
			FREE_AND_NULL(section);
			FREE_AND_NULL(key);
			return JS_FALSE;
		}
		rc=JS_SUSPENDREQUEST(cx);
		cstr2=iniReadString(p->fp,section,key,cstr,buf);
		JS_RESUMEREQUEST(cx, rc);
		JS_SET_RVAL(cx, arglist, STRING_TO_JSVAL(JS_NewStringCopyZ(cx, cstr2)));
		FREE_AND_NULL(cstr);
	}
	FREE_AND_NULL(section);
	FREE_AND_NULL(key);

	return(JS_TRUE);
}

static JSBool
js_iniSetValue_internal(JSContext *cx, JSObject *obj, uintN argc, jsval* argv, str_list_t* list)
{
	char*	section=ROOT_SECTION;
	char*	key=NULL;
	char*	result=NULL;
	int32	i;
	jsval	value=argv[2];
	private_t*	p;
	JSObject*	value_obj;
	jsrefcount	rc;
	char*		cstr;
	time_t		tt;

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)
		return(JS_TRUE);

	if(argc && argv[0]!=JSVAL_VOID && argv[0]!=JSVAL_NULL)
		JSVALUE_TO_MSTRING(cx, argv[0], section, NULL);
	JSVALUE_TO_MSTRING(cx, argv[1], key, NULL);
	if(JS_IsExceptionPending(cx)) {
		FREE_AND_NULL(section);
		FREE_AND_NULL(key);
		return JS_FALSE;
	}

	if(value==JSVAL_VOID) { 	/* unspecified value */
		rc=JS_SUSPENDREQUEST(cx);
		result = iniSetString(list,section,key,"",NULL);
		JS_RESUMEREQUEST(cx, rc);
	}
	else if(JSVAL_IS_BOOLEAN(value)) {
		result = iniSetBool(list,section,key,JSVAL_TO_BOOLEAN(value),NULL);
	}
	else if(JSVAL_IS_DOUBLE(value)) {
		result = iniSetFloat(list,section,key,JSVAL_TO_DOUBLE(value),NULL);
	}
	else if(JSVAL_IS_NUMBER(value)) {
		if(!JS_ValueToInt32(cx,value,&i)) {
			FREE_AND_NULL(section);
			FREE_AND_NULL(key);
			return JS_FALSE;
		}
		rc=JS_SUSPENDREQUEST(cx);
		result = iniSetInteger(list,section,key,i,NULL);
		JS_RESUMEREQUEST(cx, rc);
	} else if(JSVAL_IS_OBJECT(value) 
			&& (value_obj = JSVAL_TO_OBJECT(value))!=NULL
			&& (strcmp("Date",JS_GetClass(cx, value_obj)->name)==0)) {
		tt=(time_t)(js_DateGetMsecSinceEpoch(cx,value_obj)/1000.0);
		rc=JS_SUSPENDREQUEST(cx);
		result = iniSetDateTime(list,section,key,/* include_time */TRUE, tt,NULL);
		JS_RESUMEREQUEST(cx, rc);
	} else {
		cstr=NULL;
		JSVALUE_TO_MSTRING(cx, value, cstr, NULL);
		if(JS_IsExceptionPending(cx)) {
			FREE_AND_NULL(cstr);
			FREE_AND_NULL(section);
			FREE_AND_NULL(key);
			return JS_FALSE;
		}
		rc=JS_SUSPENDREQUEST(cx);
		result = iniSetString(list,section,key, cstr,NULL);
		FREE_AND_NULL(cstr);
		JS_RESUMEREQUEST(cx, rc);
	}
	FREE_AND_NULL(section);
	FREE_AND_NULL(key);

	return(result != NULL);
}

static JSBool
js_iniSetValue(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	jsval	rval=JSVAL_FALSE;
	private_t*	p;
	str_list_t	list;
	jsrefcount	rc;

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)  
		return(JS_TRUE);

	rc=JS_SUSPENDREQUEST(cx);
	if((list=iniReadFile(p->fp)) != NULL) {
		if(js_iniSetValue_internal(cx, obj, argc, argv, &list))
			rval = BOOLEAN_TO_JSVAL(iniWriteFile(p->fp,list));
	}
	strListFree(&list);
	JS_RESUMEREQUEST(cx, rc);

	JS_SET_RVAL(cx, arglist, rval);
	return JS_TRUE;
}

static JSBool
js_iniRemoveKey(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	char*	section=ROOT_SECTION;
	char*	key=NULL;
	private_t*	p;
	str_list_t	list;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_FALSE);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)
		return(JS_TRUE);

	if(argc && argv[0]!=JSVAL_VOID && argv[0]!=JSVAL_NULL)
		JSVALUE_TO_MSTRING(cx, argv[0], section, NULL);
	JSVALUE_TO_MSTRING(cx, argv[1], key, NULL);
	HANDLE_PENDING(cx);
	if(key==NULL) {
		JS_ReportError(cx, "Invalid NULL key specified");
		FREE_AND_NULL(section);
		return JS_FALSE;
	}

	rc=JS_SUSPENDREQUEST(cx);
	if((list=iniReadFile(p->fp))==NULL) {
		JS_RESUMEREQUEST(cx, rc);
		FREE_AND_NULL(section);
		FREE_AND_NULL(key);
		return(JS_TRUE);
	}

	if(iniRemoveKey(&list,section,key))
		JS_SET_RVAL(cx, arglist, BOOLEAN_TO_JSVAL(iniWriteFile(p->fp,list)));

	FREE_AND_NULL(section);
	FREE_AND_NULL(key);

	strListFree(&list);
	JS_RESUMEREQUEST(cx, rc);

	return(JS_TRUE);
}

static JSBool
js_iniRemoveSection(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	char*	section=ROOT_SECTION;
	private_t*	p;
	str_list_t	list;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_FALSE);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)
		return(JS_TRUE);

	if(argc && argv[0]!=JSVAL_VOID && argv[0]!=JSVAL_NULL) {
		JSVALUE_TO_MSTRING(cx, argv[0], section, NULL);
		HANDLE_PENDING(cx);
	}

	rc=JS_SUSPENDREQUEST(cx);
	if((list=iniReadFile(p->fp))==NULL) {
		JS_RESUMEREQUEST(cx, rc);
		FREE_AND_NULL(section);
		return(JS_TRUE);
	}

	if(iniRemoveSection(&list,section))
		JS_SET_RVAL(cx, arglist, BOOLEAN_TO_JSVAL(iniWriteFile(p->fp,list)));

	FREE_AND_NULL(section);
	strListFree(&list);
	JS_RESUMEREQUEST(cx, rc);

	return(JS_TRUE);
}


static JSBool
js_iniGetSections(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	char*		prefix=NULL;
	char**		list;
    jsint       i;
    jsval       val;
    JSObject*	array;
	private_t*	p;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_NULL);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)
		return(JS_TRUE);

	if(argc) {
		JSVALUE_TO_MSTRING(cx, argv[0], prefix, NULL);
		HANDLE_PENDING(cx);
	}

    array = JS_NewArrayObject(cx, 0, NULL);

	rc=JS_SUSPENDREQUEST(cx);
	list = iniReadSectionList(p->fp,prefix);
	FREE_AND_NULL(prefix);
	JS_RESUMEREQUEST(cx, rc);
    for(i=0;list && list[i];i++) {
		val=STRING_TO_JSVAL(JS_NewStringCopyZ(cx,list[i]));
        if(!JS_SetElement(cx, array, i, &val))
			break;
	}
	rc=JS_SUSPENDREQUEST(cx);
	iniFreeStringList(list);
	JS_RESUMEREQUEST(cx, rc);

    JS_SET_RVAL(cx, arglist, OBJECT_TO_JSVAL(array));

    return(JS_TRUE);
}

static JSBool
js_iniGetKeys(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	char*		section=ROOT_SECTION;
	char**		list;
    jsint       i;
    jsval       val;
    JSObject*	array;
	private_t*	p;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_NULL);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)
		return(JS_TRUE);

	if(argc && argv[0]!=JSVAL_VOID && argv[0]!=JSVAL_NULL) {
		JSVALUE_TO_MSTRING(cx, argv[0], section, NULL);
		HANDLE_PENDING(cx);
	}
    array = JS_NewArrayObject(cx, 0, NULL);

	rc=JS_SUSPENDREQUEST(cx);
	list = iniReadKeyList(p->fp,section);
	FREE_AND_NULL(section);
	JS_RESUMEREQUEST(cx, rc);
    for(i=0;list && list[i];i++) {
		val=STRING_TO_JSVAL(JS_NewStringCopyZ(cx,list[i]));
        if(!JS_SetElement(cx, array, i, &val))
			break;
	}
	rc=JS_SUSPENDREQUEST(cx);
	iniFreeStringList(list);
	JS_RESUMEREQUEST(cx, rc);

    JS_SET_RVAL(cx, arglist, OBJECT_TO_JSVAL(array));

    return(JS_TRUE);
}

static JSBool
js_iniGetObject(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	char*		section=ROOT_SECTION;
    jsint       i;
    JSObject*	object;
	private_t*	p;
	named_string_t** list;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_NULL);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)
		return(JS_TRUE);

	if(argc>0 && argv[0]!=JSVAL_VOID && argv[0]!=JSVAL_NULL) {
		JSVALUE_TO_MSTRING(cx, argv[0], section, NULL);
		HANDLE_PENDING(cx);
	}

	rc=JS_SUSPENDREQUEST(cx);
	list = iniReadNamedStringList(p->fp,section);
	FREE_AND_NULL(section);
	JS_RESUMEREQUEST(cx, rc);

	if(list==NULL) {	/* New behavior at request of MCMLXXIX: return NULL if specified section doesn't exist */
		JS_SET_RVAL(cx, arglist, JSVAL_NULL);
		return(JS_TRUE);
	}

    object = JS_NewObject(cx, NULL, NULL, obj);

    for(i=0;list && list[i];i++) {
		JS_DefineProperty(cx, object, list[i]->name
			,get_value(cx,list[i]->value)
			,NULL,NULL,JSPROP_ENUMERATE);

	}
	rc=JS_SUSPENDREQUEST(cx);
	iniFreeNamedStringList(list);
	JS_RESUMEREQUEST(cx, rc);

    JS_SET_RVAL(cx, arglist, OBJECT_TO_JSVAL(object));

    return(JS_TRUE);
}

static JSBool
js_iniSetObject(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
    jsint       i;
    JSObject*	object;
	JSIdArray*	id_array;
	jsval		set_argv[3];
	jsval		rval;
	char*		cp;
	private_t*	p;
	str_list_t	list;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_FALSE);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)  
		return(JS_TRUE);

	set_argv[0]=argv[0];	/* section */

	if(!JSVAL_IS_OBJECT(argv[1]) || argv[1]==JSVAL_NULL)
		return(JS_TRUE);

    object = JSVAL_TO_OBJECT(argv[1]);

	if((id_array=JS_Enumerate(cx,object))==NULL)
		return(JS_TRUE);

	rc=JS_SUSPENDREQUEST(cx);
	if((list=iniReadFile(p->fp))==NULL) {
		JS_RESUMEREQUEST(cx, rc);
		JS_DestroyIdArray(cx,id_array);
		return JS_TRUE;
	}
	JS_RESUMEREQUEST(cx, rc);

	rval = JSVAL_TRUE;
	for(i=0; i<id_array->length; i++)  {
		/* property */
		JS_IdToValue(cx,id_array->vector[i],&set_argv[1]);	
		/* value */
		cp=NULL;
		JSVALUE_TO_MSTRING(cx, set_argv[1], cp, NULL);
		if(cp==NULL) {
			JS_DestroyIdArray(cx,id_array);
			JS_ReportError(cx, "Invalid NULL property");
			return JS_FALSE;
		}
		if(JS_IsExceptionPending(cx)) {
			FREE_AND_NULL(cp);
			JS_DestroyIdArray(cx,id_array);
			return JS_FALSE;
		}
		JS_GetProperty(cx,object,cp,&set_argv[2]);
		FREE_AND_NULL(cp);
		if(!js_iniSetValue_internal(cx,obj,3,set_argv,&list)) {
			rval = JSVAL_FALSE;
			break;
		}
	}

	rc=JS_SUSPENDREQUEST(cx);
	if(rval == JSVAL_TRUE)
		rval = BOOLEAN_TO_JSVAL(iniWriteFile(p->fp,list));
	strListFree(&list);
	JS_RESUMEREQUEST(cx, rc);
	
	JS_SET_RVAL(cx, arglist, rval);

	JS_DestroyIdArray(cx,id_array);

    return(JS_TRUE);
}


static JSBool
js_iniGetAllObjects(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	const char	*name_def="name";
	char*		name=(char *)name_def;
	char*		sec_name;
	char*		prefix=NULL;
	char**		sec_list;
    jsint       i,k;
    jsval       val;
    JSObject*	array;
    JSObject*	object;
	private_t*	p;
	named_string_t** key_list;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_NULL);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)
		return(JS_TRUE);

	if(argc)
		JSVALUE_TO_MSTRING(cx, argv[0], name, NULL);
	HANDLE_PENDING(cx);
	if(name == NULL) {
		JS_ReportError(cx, "Invalid NULL name property");
		return JS_FALSE;
	}

	if(argc>1)
		JSVALUE_TO_MSTRING(cx, argv[1], prefix, NULL);
	if(JS_IsExceptionPending(cx)) {
		FREE_AND_NULL(prefix);
		if(name != name_def)
			free(name);
		return JS_FALSE;
	}

    array = JS_NewArrayObject(cx, 0, NULL);

	rc=JS_SUSPENDREQUEST(cx);
	sec_list = iniReadSectionList(p->fp,prefix);
	JS_RESUMEREQUEST(cx, rc);
    for(i=0;sec_list && sec_list[i];i++) {
	    object = JS_NewObject(cx, NULL, NULL, obj);

		sec_name=sec_list[i];
		if(prefix!=NULL)
			sec_name+=strlen(prefix);
		JS_DefineProperty(cx, object, name
			,STRING_TO_JSVAL(JS_NewStringCopyZ(cx,sec_name))
			,NULL,NULL,JSPROP_ENUMERATE);

		rc=JS_SUSPENDREQUEST(cx);
		key_list = iniReadNamedStringList(p->fp,sec_list[i]);
		JS_RESUMEREQUEST(cx, rc);
		for(k=0;key_list && key_list[k];k++)
			JS_DefineProperty(cx, object, key_list[k]->name
				,get_value(cx,key_list[k]->value)
				,NULL,NULL,JSPROP_ENUMERATE);
		rc=JS_SUSPENDREQUEST(cx);
		iniFreeNamedStringList(key_list);
		JS_RESUMEREQUEST(cx, rc);

		val=OBJECT_TO_JSVAL(object);
		/* exception here, Apr-4-2010:

  2000007a()
js_iniGetAllObjects(JSContext * 0x049383e0, JSObject * 0x049c76a8, unsigned int 0x00000001, long * 0x049c0490, long * 0x02c5c494) line 1064 + 24 bytes
js_Invoke(JSContext * 0x049383e0, unsigned int 0x00000001, unsigned int 0x00000000) line 1375 + 23 bytes
js_Interpret(JSContext * 0x049383e0, unsigned char * 0x031ab4b2, long * 0x02c5d6ac) line 3944 + 15 bytes
js_Execute(JSContext * 0x049383e0, JSObject * 0x049b73e8, JSObject * 0x02f2a7e0, JSStackFrame * 0x00000000, unsigned int 0x00000000, long * 0x02c5d7bc) line 1633 + 19 bytes
JS_ExecuteScript(JSContext * 0x049383e0, JSObject * 0x049b73e8, JSObject * 0x02f2a7e0, long * 0x02c5d7bc) line 4188 + 25 bytes
sbbs_t::js_execfile(const char * 0x0226b59a, const char * 0x022060fa) line 668 + 39 bytes
sbbs_t::external(const char * 0x0226b599, long 0x00000100, const char * 0x022060fa) line 413 + 30 bytes
event_thread(void * 0x022622b8) line 2745 + 113 bytes
_threadstart(void * 0x0227dab0) line 187 + 13 bytes


and July-15-2010:

 	20000000()	
 	js32.dll!JS_SetElement(JSContext * cx, JSObject * obj, long index, long * vp)  Line 3178 + 0x20 bytes	C
>	sbbs.dll!js_iniGetAllObjects(JSContext * cx, JSObject * obj, unsigned int argc, long * argv, long * rval)  Line 1081 + 0x18 bytes	C
 	js32.dll!js_Invoke(JSContext * cx, unsigned int argc, unsigned int flags)  Line 1375 + 0x17 bytes	C
 	js32.dll!js_Interpret(JSContext * cx, unsigned char * pc, long * result)  Line 3944 + 0xf bytes	C
 	js32.dll!js_Execute(JSContext * cx, JSObject * chain, JSObject * script, JSStackFrame * down, unsigned int flags, long * result)  Line 1633 + 0x13 bytes	C
 	js32.dll!JS_ExecuteScript(JSContext * cx, JSObject * obj, JSObject * script, long * rval)  Line 4188 + 0x19 bytes	C
 	sbbs.dll!sbbs_t::js_execfile(const char * cmd, const char * startup_dir)  Line 686 + 0x27 bytes	C++
 	sbbs.dll!sbbs_t::external(const char * cmdline, long mode, const char * startup_dir)  Line 413 + 0x1e bytes	C++
 	sbbs.dll!event_thread(void * arg)  Line 2745 + 0x71 bytes	C++

And July-22-2010:

 	js32.dll!JS_SetElement(JSContext * cx, JSObject * obj, long index, long * vp)  Line 3178 + 0x20 bytes	C
>	sbbs.dll!js_iniGetAllObjects(JSContext * cx, JSObject * obj, unsigned int argc, long * argv, long * rval)  Line 1095 + 0x18 bytes	C
 	js32.dll!js_Invoke(JSContext * cx, unsigned int argc, unsigned int flags)  Line 1375 + 0x17 bytes	C
 	js32.dll!js_Interpret(JSContext * cx, unsigned char * pc, long * result)  Line 3944 + 0xf bytes	C
 	js32.dll!js_Execute(JSContext * cx, JSObject * chain, JSObject * script, JSStackFrame * down, unsigned int flags, long * result)  Line 1633 + 0x13 bytes	C
 	js32.dll!JS_ExecuteScript(JSContext * cx, JSObject * obj, JSObject * script, long * rval)  Line 4188 + 0x19 bytes	C
 	websrvr.dll!exec_ssjs(http_session_t * session, char * script)  Line 4638 + 0x24 bytes	C
 	websrvr.dll!respond(http_session_t * session)  Line 4684 + 0x12 bytes	C
 	websrvr.dll!http_session_thread(void * arg)  Line 5091 + 0xc bytes	C


And Sept-17-2011:

JS_SetElement(JSContext * 0x058b7138, JSObject * 0x0578cd78, long 2, long * 0x084cb338) line 3178 + 32 bytes
js_iniGetAllObjects(JSContext * 0x058b7138, JSObject * 0x0578cd60, unsigned int 0, long * 0x063adfbc, long * 0x084cb460) line 1115 + 24 bytes
js_Invoke(JSContext * 0x058b7138, unsigned int 0, unsigned int 0) line 1375 + 23 bytes
js_Interpret(JSContext * 0x058b7138, unsigned char * 0x04e9298b, long * 0x084cc678) line 3944 + 15 bytes
js_Execute(JSContext * 0x058b7138, JSObject * 0x057b4b48, JSScript * 0x04e92880, JSStackFrame * 0x00000000, unsigned int 0, long * 0x084cc860) line 1633 + 19 bytes
JS_ExecuteScript(JSContext * 0x058b7138, JSObject * 0x057b4b48, JSScript * 0x04e92880, long * 0x084cc860) line 4188 + 25 bytes
exec_ssjs(http_session_t * 0x084ce0f0, char * 0x084ce221) line 4644 + 36 bytes
respond(http_session_t * 0x084ce0f0) line 4690 + 18 bytes
http_session_thread(void * 0x00000000) line 5098 + 12 bytes
  */
        if(!JS_SetElement(cx, array, i, &val))
			break;
	}
	rc=JS_SUSPENDREQUEST(cx);
	FREE_AND_NULL(prefix);
	if(name != name_def)
		free(name);
	iniFreeStringList(sec_list);
	JS_RESUMEREQUEST(cx, rc);

    JS_SET_RVAL(cx, arglist, OBJECT_TO_JSVAL(array));

    return(JS_TRUE);
}

static JSBool
js_iniSetAllObjects(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	const char	*name_def = "name";
	char*		name=(char *)name_def;
    jsuint      i;
    jsint       j;
    jsuint      count;
    JSObject*	array;
    JSObject*	object;
	jsval		oval;
	jsval		set_argv[3];
	JSIdArray*	id_array;
	jsval		rval;
	char*		cp=NULL;
	str_list_t	list;
	jsrefcount	rc;
	private_t*	p;

	JS_SET_RVAL(cx, arglist, JSVAL_FALSE);

	if(JSVAL_IS_NULL(argv[0]) || !JSVAL_IS_OBJECT(argv[0]))
		return(JS_TRUE);

    array = JSVAL_TO_OBJECT(argv[0]);

	if(!JS_IsArrayObject(cx, array))
		return(JS_TRUE);

    if(!JS_GetArrayLength(cx, array, &count))
		return(JS_TRUE);

	if(argc>1)
		JSVALUE_TO_MSTRING(cx, argv[1], name, NULL);
	HANDLE_PENDING(cx);
	if(name==NULL) {
		JS_ReportError(cx, "Invalid NULL name property");
		return JS_FALSE;
	}

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)  
		return(JS_TRUE);

	rc=JS_SUSPENDREQUEST(cx);
	if((list=iniReadFile(p->fp))==NULL) {
		JS_RESUMEREQUEST(cx, rc);
		return JS_TRUE;
	}
	JS_RESUMEREQUEST(cx, rc);

	/* enumerate the array */
	rval = JSVAL_TRUE;
	for(i=0; i<count && rval==JSVAL_TRUE; i++)  {
        if(!JS_GetElement(cx, array, i, &oval))
			break;
		if(!JSVAL_IS_OBJECT(oval))	/* must be an array of objects */
			break;
		object=JSVAL_TO_OBJECT(oval);
		if(!JS_GetProperty(cx, object, name, &set_argv[0]))
			continue;
		if((id_array=JS_Enumerate(cx,object))==NULL) {
			if(name != name_def)
				free(name);
			return(JS_TRUE);
		}

		for(j=0; j<id_array->length; j++)  {
			/* property */
			JS_IdToValue(cx,id_array->vector[j],&set_argv[1]);	
			/* check if not name */
			JSVALUE_TO_MSTRING(cx, set_argv[1], cp, NULL);
			if(JS_IsExceptionPending(cx)) {
				FREE_AND_NULL(cp);
				JS_DestroyIdArray(cx,id_array);
				if(name != name_def)
					free(name);
				return JS_FALSE;
			}
			if(cp==NULL)
				continue;
			if(strcmp(cp,name)==0) {
				FREE_AND_NULL(cp);
				continue;
			}
			/* value */
			JS_GetProperty(cx,object,cp,&set_argv[2]);
			FREE_AND_NULL(cp);	/* Moved from before JS_GetProperty() call */
			if(!js_iniSetValue_internal(cx,obj,3,set_argv,&list)) {
				rval = JSVAL_FALSE;
				break;
			}
		}
		JS_DestroyIdArray(cx,id_array);
	}
	if(name != name_def)
		free(name);

	rc=JS_SUSPENDREQUEST(cx);
	if(rval == JSVAL_TRUE)
		rval = BOOLEAN_TO_JSVAL(iniWriteFile(p->fp,list));
	strListFree(&list);
	JS_RESUMEREQUEST(cx, rc);

	JS_SET_RVAL(cx, arglist, rval);

    return(JS_TRUE);
}

static JSBool
js_write(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	char*		cp;
	char*		uubuf=NULL;
	size_t		len;	/* string length */
	size_t		tlen;	/* total length to write (may be greater than len) */
	int32		i;
	JSString*	str;
	private_t*	p;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_FALSE);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)
		return(JS_TRUE);

	if((str = JS_ValueToString(cx, argv[0]))==NULL)
		return(JS_FALSE);

	JSSTRING_TO_MSTRING(cx, str, cp, &len);
	HANDLE_PENDING(cx);
	if(cp==NULL)
		return JS_TRUE;

	rc=JS_SUSPENDREQUEST(cx);
	if((p->uuencoded || p->b64encoded || p->yencoded)
		&& len && (uubuf=malloc(len))!=NULL) {
		if(p->uuencoded)
			len=uudecode(uubuf,len,cp,len);
		else if(p->yencoded)
			len=ydecode(uubuf,len,cp,len);
		else
			len=b64_decode(uubuf,len,cp,len);
		if(len<0) {
			free(uubuf);
			free(cp);
			JS_RESUMEREQUEST(cx, rc);
			return(JS_TRUE);
		}
		free(cp);
		cp=uubuf;
	}

	if(p->rot13)
		rot13(cp);

	JS_RESUMEREQUEST(cx, rc);
	tlen=len;
	if(argc>1) {
		if(!JS_ValueToInt32(cx,argv[1],&i)) {
			free(cp);
			return(JS_FALSE);
		}
		tlen=i;
		if(len>tlen)
			len=tlen;
	}

	rc=JS_SUSPENDREQUEST(cx);
	if(fwrite(cp,1,len,p->fp)==(size_t)len) {
		free(cp);
		if(tlen>len) {
			len=tlen-len;
			if((cp=malloc(len))==NULL) {
				JS_RESUMEREQUEST(cx, rc);
				JS_ReportError(cx, "malloc failure of %u bytes", len);
				return(JS_FALSE);
			}
			memset(cp,p->etx,len);
			if(fwrite(cp,1,len,p->fp) < len) {
				free(cp);
				JS_RESUMEREQUEST(cx, rc);
				return JS_TRUE;
			}
			free(cp);
		}
		dbprintf(FALSE, p, "wrote %u bytes",tlen);
		JS_SET_RVAL(cx, arglist, JSVAL_TRUE);
	} else {
		free(cp);
		dbprintf(TRUE, p, "write of %u bytes failed",len);
	}

	JS_RESUMEREQUEST(cx, rc);
		
	return(JS_TRUE);
}

static JSBool
js_writeln_internal(JSContext *cx, JSObject *obj, jsval *arg, jsval *rval)
{
	const char	*cp_def="";
	char*		cp=(char *)cp_def;
	JSString*	str;
	private_t*	p;
	jsrefcount	rc;

	*rval = JSVAL_FALSE;

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)
		return(JS_TRUE);

	if(arg) {
		if((str = JS_ValueToString(cx, *arg))==NULL) {
			JS_ReportError(cx,"JS_ValueToString failed");
			return(JS_FALSE);
		}
		JSSTRING_TO_MSTRING(cx, str, cp, NULL);
		HANDLE_PENDING(cx);
		if(cp==NULL)
			cp=(char *)cp_def;
	}

	rc=JS_SUSPENDREQUEST(cx);
	if(p->rot13)
		rot13(cp);

	if(fprintf(p->fp,"%s\n",cp)!=0)
		*rval = JSVAL_TRUE;
	if(cp != cp_def)
		free(cp);
	JS_RESUMEREQUEST(cx, rc);

	return(JS_TRUE);
}

static JSBool
js_writeln(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	jsval	rval;
	JSBool	ret;

	if(argc) {
		ret=js_writeln_internal(cx, obj, &argv[0], &rval);
	}
	else {
		ret=js_writeln_internal(cx, obj, NULL, &rval);
	}
	JS_SET_RVAL(cx, arglist, rval);

	return(ret);
}

static JSBool
js_writebin(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	BYTE		*b;
	WORD		*w;
	DWORD		*l;
	size_t		wr=0;
	int32		size=sizeof(DWORD);
	jsuint		count=1;
	void		*buffer;
	private_t*	p;
    JSObject*	array=NULL;
    jsval       elemval;
	jsdouble	val=0;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_FALSE);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)
		return(JS_TRUE);

	if(JSVAL_IS_OBJECT(argv[0]) && !JSVAL_IS_NULL(argv[0])) {
		array = JSVAL_TO_OBJECT(argv[0]);
		if(JS_IsArrayObject(cx, array)) {
		    if(!JS_GetArrayLength(cx, array, &count))
				return(JS_TRUE);
		}
		else
			array=NULL;
	}
	if(array==NULL) {
		if(!JS_ValueToNumber(cx,argv[0],&val))
			return(JS_FALSE);
	}
	if(argc>1) {
		if(!JS_ValueToInt32(cx,argv[1],&size))
			return(JS_FALSE);
	}
	if(size != sizeof(BYTE) && size != sizeof(WORD) && size != sizeof(DWORD)) {
		rc=JS_SUSPENDREQUEST(cx);
		dbprintf(TRUE, p, "unsupported binary write size: %d",size);
		JS_RESUMEREQUEST(cx, rc);
		return(JS_TRUE);
	}
	buffer=malloc(size*count);
	if(buffer==NULL) {
		rc=JS_SUSPENDREQUEST(cx);
		dbprintf(TRUE, p, "malloc failure of %u bytes", size*count);
		JS_RESUMEREQUEST(cx, rc);
		return(JS_FALSE);
	}
	b=buffer;
	w=buffer;
	l=buffer;
	if(array==NULL) {
		switch(size) {
			case sizeof(BYTE):
				*b=(BYTE)val;
				break;
			case sizeof(WORD):
				*w=(WORD)val;
				break;
			case sizeof(DWORD):
				*l=(DWORD)val;
				break;
		}
	}
	else {
		for(wr=0; wr<count; wr++) {
	        if(!JS_GetElement(cx, array, wr, &elemval))
				goto end;
			if(!JS_ValueToNumber(cx,elemval,&val))
				goto end;
			switch(size) {
				case sizeof(BYTE):
					*(b++)=(BYTE)val;
					break;
				case sizeof(WORD):
					*(w++)=(WORD)val;
					break;
				case sizeof(DWORD):
					*(l++)=(DWORD)val;
					break;
			}
		}
	}
	rc=JS_SUSPENDREQUEST(cx);
	wr=fwrite(buffer,size,count,p->fp);
	JS_RESUMEREQUEST(cx, rc);
	if(wr==count)
		JS_SET_RVAL(cx, arglist,JSVAL_TRUE);

end:
	free(buffer);
	return(JS_TRUE);
}

static JSBool
js_writeall(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
    jsuint      i;
    jsuint      limit;
    JSObject*	array;
    jsval       elemval;
	private_t*	p;

	JS_SET_RVAL(cx, arglist, JSVAL_FALSE);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)
		return(JS_TRUE);

	if(JSVAL_IS_NULL(argv[0]) || !JSVAL_IS_OBJECT(argv[0]))
		return(JS_TRUE);

    array = JSVAL_TO_OBJECT(argv[0]);

    if(!JS_IsArrayObject(cx, array))
		return(JS_TRUE);

    if(!JS_GetArrayLength(cx, array, &limit))
		return(JS_FALSE);

    JS_SET_RVAL(cx, arglist, JSVAL_TRUE);

    for(i=0;i<limit;i++) {
		jsval	rval;

        if(!JS_GetElement(cx, array, i, &elemval))
			break;
        js_writeln_internal(cx, obj, &elemval, &rval);
        JS_SET_RVAL(cx, arglist, rval);
		if(rval!=JSVAL_TRUE)
			break;
    }

    return(JS_TRUE);
}

static JSBool
js_lock(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	off_t		offset=0;
	off_t		len=0;
	private_t*	p;
	jsrefcount	rc;
	jsdouble	val;

	JS_SET_RVAL(cx, arglist, JSVAL_FALSE);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)
		return(JS_TRUE);

	/* offset */
	if(argc) {
		if(!JS_ValueToNumber(cx,argv[0],&val))
			return(JS_FALSE);
		offset=(off_t)val;
	}

	/* length */
	if(argc>1) {
		if(!JS_ValueToNumber(cx,argv[1],&val))
			return(JS_FALSE);
		len=(off_t)val;
	}

	rc=JS_SUSPENDREQUEST(cx);
	if(len==0)
		len=filelength(fileno(p->fp))-offset;

	if(lock(fileno(p->fp),offset,len)==0)
		JS_SET_RVAL(cx, arglist, JSVAL_TRUE);
	JS_RESUMEREQUEST(cx, rc);

	return(JS_TRUE);
}

static JSBool
js_unlock(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	off_t		offset=0;
	off_t		len=0;
	private_t*	p;
	jsrefcount	rc;
	jsdouble	val;

	JS_SET_RVAL(cx, arglist, JSVAL_FALSE);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)
		return(JS_TRUE);

	/* offset */
	if(argc) {
		if(!JS_ValueToNumber(cx,argv[0],&val))
			return(JS_FALSE);
		offset=(off_t)val;
	}

	/* length */
	if(argc>1) {
		if(!JS_ValueToNumber(cx,argv[1],&val))
			return(JS_FALSE);
		len=(off_t)val;
	}

	rc=JS_SUSPENDREQUEST(cx);
	if(len==0)
		len=filelength(fileno(p->fp))-offset;

	if(unlock(fileno(p->fp),offset,len)==0)
		JS_SET_RVAL(cx, arglist, JSVAL_TRUE);
	JS_RESUMEREQUEST(cx, rc);

	return(JS_TRUE);
}

static JSBool
js_delete(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	private_t*	p;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_VOID);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp!=NULL) {	/* close it if it's open */
		fclose(p->fp);
		p->fp=NULL;
	}

	rc=JS_SUSPENDREQUEST(cx);
	JS_SET_RVAL(cx, arglist, BOOLEAN_TO_JSVAL(remove(p->name)==0));
	JS_RESUMEREQUEST(cx, rc);

	return(JS_TRUE);
}

static JSBool
js_flush(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	private_t*	p;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_VOID);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	rc=JS_SUSPENDREQUEST(cx);
	if(p->fp==NULL)
		JS_SET_RVAL(cx, arglist, JSVAL_FALSE);
	else 
		JS_SET_RVAL(cx, arglist, BOOLEAN_TO_JSVAL(fflush(p->fp)==0));
	JS_RESUMEREQUEST(cx, rc);

	return(JS_TRUE);
}

static JSBool
js_rewind(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	private_t*	p;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_VOID);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	rc=JS_SUSPENDREQUEST(cx);
	if(p->fp==NULL)
		JS_SET_RVAL(cx, arglist, JSVAL_FALSE);
	else  {
		JS_SET_RVAL(cx, arglist, JSVAL_TRUE);
		rewind(p->fp);
	}
	JS_RESUMEREQUEST(cx, rc);

	return(JS_TRUE);
}

static JSBool
js_truncate(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	private_t*	p;
	int32		len=0;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_VOID);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(argc) {
		if(!JS_ValueToInt32(cx,argv[0],&len))
			return(JS_FALSE);
	}

	rc=JS_SUSPENDREQUEST(cx);
	JS_SET_RVAL(cx, arglist, JSVAL_FALSE);
	if(p->fp!=NULL && chsize(fileno(p->fp),len)==0) {
		fseek(p->fp,len,SEEK_SET);
		JS_SET_RVAL(cx, arglist, JSVAL_TRUE);
	}
	JS_RESUMEREQUEST(cx, rc);

	return(JS_TRUE);
}

static JSBool
js_clear_error(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	private_t*	p;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_VOID);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	rc=JS_SUSPENDREQUEST(cx);
	if(p->fp==NULL)
		JS_SET_RVAL(cx, arglist, JSVAL_FALSE);
	else  {
		clearerr(p->fp);
		JS_SET_RVAL(cx, arglist, JSVAL_TRUE);
	}
	JS_RESUMEREQUEST(cx, rc);

	return(JS_TRUE);
}

static JSBool
js_fprintf(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	char*		cp;
	private_t*	p;
	jsrefcount	rc;

	JS_SET_RVAL(cx, arglist, JSVAL_FALSE);

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

	if(p->fp==NULL)
		return(JS_TRUE);

	if((cp=js_sprintf(cx, 0, argc, argv))==NULL) {
		JS_ReportError(cx,"js_sprintf failed");
		return(JS_FALSE);
	}

	rc=JS_SUSPENDREQUEST(cx);
	JS_SET_RVAL(cx, arglist, INT_TO_JSVAL(fwrite(cp,1,strlen(cp),p->fp)));
	JS_RESUMEREQUEST(cx, rc);
	js_sprintf_free(cp);
	
    return(JS_TRUE);
}


/* File Object Properites */
enum {
	 FILE_PROP_NAME		
	,FILE_PROP_MODE
	,FILE_PROP_ETX
	,FILE_PROP_EXISTS	
	,FILE_PROP_DATE		
	,FILE_PROP_IS_OPEN	
	,FILE_PROP_EOF		
	,FILE_PROP_ERROR	
	,FILE_PROP_DESCRIPTOR
	,FILE_PROP_DEBUG	
	,FILE_PROP_POSITION	
	,FILE_PROP_LENGTH	
	,FILE_PROP_ATTRIBUTES
	,FILE_PROP_YENCODED
	,FILE_PROP_UUENCODED
	,FILE_PROP_B64ENCODED
	,FILE_PROP_ROT13
	,FILE_PROP_NETWORK_ORDER
	/* dynamically calculated */
	,FILE_PROP_CHKSUM
	,FILE_PROP_CRC16
	,FILE_PROP_CRC32
	,FILE_PROP_MD5_HEX
	,FILE_PROP_MD5_B64
};


static JSBool js_file_set(JSContext *cx, JSObject *obj, jsid id, JSBool strict, jsval *vp)
{
	jsval idval;
	int32		i=0;
    jsint       tiny;
	private_t*	p;
	jsrefcount	rc;

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL) {
		JS_ReportError(cx,getprivate_failure,WHERE);
		return(JS_FALSE);
	}

    JS_IdToValue(cx, id, &idval);
    tiny = JSVAL_TO_INT(idval);

	rc=JS_SUSPENDREQUEST(cx);
	dbprintf(FALSE, p, "setting property %d",tiny);
	JS_RESUMEREQUEST(cx, rc);

	switch(tiny) {
		case FILE_PROP_DEBUG:
			JS_ValueToBoolean(cx,*vp,&(p->debug));
			break;
		case FILE_PROP_YENCODED:
			JS_ValueToBoolean(cx,*vp,&(p->yencoded));
			break;
		case FILE_PROP_UUENCODED:
			JS_ValueToBoolean(cx,*vp,&(p->uuencoded));
			break;
		case FILE_PROP_B64ENCODED:
			JS_ValueToBoolean(cx,*vp,&(p->b64encoded));
			break;
		case FILE_PROP_ROT13:
			JS_ValueToBoolean(cx,*vp,&(p->rot13));
			break;
		case FILE_PROP_NETWORK_ORDER:
			JS_ValueToBoolean(cx,*vp,&(p->network_byte_order));
			break;
		case FILE_PROP_POSITION:
			if(p->fp!=NULL) {
				if(!JS_ValueToInt32(cx,*vp,&i))
					return(JS_FALSE);
				rc=JS_SUSPENDREQUEST(cx);
				fseek(p->fp,i,SEEK_SET);
				JS_RESUMEREQUEST(cx, rc);
			}
			break;
		case FILE_PROP_DATE:
			if(!JS_ValueToInt32(cx,*vp,&i))
				return(JS_FALSE);
			rc=JS_SUSPENDREQUEST(cx);
			setfdate(p->name,i);
			JS_RESUMEREQUEST(cx, rc);
			break;
		case FILE_PROP_LENGTH:
			if(p->fp!=NULL) {
				if(!JS_ValueToInt32(cx,*vp,&i))
					return(JS_FALSE);
				rc=JS_SUSPENDREQUEST(cx);
				chsize(fileno(p->fp),i);
				JS_RESUMEREQUEST(cx, rc);
			}
			break;
		case FILE_PROP_ATTRIBUTES:
			if(!JS_ValueToInt32(cx,*vp,&i))
				return(JS_FALSE);
			rc=JS_SUSPENDREQUEST(cx);
			CHMOD(p->name,i);
			JS_RESUMEREQUEST(cx, rc);
			break;
		case FILE_PROP_ETX:
			if(!JS_ValueToInt32(cx,*vp,&i))
				return(JS_FALSE);
			p->etx = (uchar)i;
			break;
	}

	return(JS_TRUE);
}

static JSBool js_file_get(JSContext *cx, JSObject *obj, jsid id, jsval *vp)
{
	jsval idval;
	char		str[128];
	size_t		i;
	size_t		rd;
	off_t		offset;
	ulong		sum=0;
	ushort		c16=0;
	ulong		c32=~0;
	MD5			md5_ctx;
	BYTE		block[4096];
	BYTE		digest[MD5_DIGEST_SIZE];
    jsint       tiny;
	JSString*	js_str=NULL;
	private_t*	p;
	jsrefcount	rc;
	time_t		tt;
	off_t		lng;
	int			in;

	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL)
		return(JS_FALSE);

    JS_IdToValue(cx, id, &idval);
    tiny = JSVAL_TO_INT(idval);

	switch(tiny) {
		case FILE_PROP_NAME:
			if((js_str=JS_NewStringCopyZ(cx, p->name))==NULL)
				return(JS_FALSE);
			*vp = STRING_TO_JSVAL(js_str);
			break;
		case FILE_PROP_MODE:
			if((js_str=JS_NewStringCopyZ(cx, p->mode))==NULL)
				return(JS_FALSE);
			*vp = STRING_TO_JSVAL(js_str);
			break;
		case FILE_PROP_EXISTS:
			if(p->fp)	/* open? */
				*vp = JSVAL_TRUE;
			else {
				rc=JS_SUSPENDREQUEST(cx);
				*vp = BOOLEAN_TO_JSVAL(fexistcase(p->name));
				JS_RESUMEREQUEST(cx, rc);
			}
			break;
		case FILE_PROP_DATE:
			rc=JS_SUSPENDREQUEST(cx);
			tt=fdate(p->name);
			JS_RESUMEREQUEST(cx, rc);
			*vp=DOUBLE_TO_JSVAL((double)tt);
			break;
		case FILE_PROP_IS_OPEN:
			*vp = BOOLEAN_TO_JSVAL(p->fp!=NULL);
			break;
		case FILE_PROP_EOF:
			if(p->fp) {
				rc=JS_SUSPENDREQUEST(cx);
				*vp = BOOLEAN_TO_JSVAL(feof(p->fp)!=0);
				JS_RESUMEREQUEST(cx, rc);
			}
			else
				*vp = JSVAL_TRUE;
			break;
		case FILE_PROP_ERROR:
			if(p->fp) {
				rc=JS_SUSPENDREQUEST(cx);
				*vp = INT_TO_JSVAL(ferror(p->fp));
				JS_RESUMEREQUEST(cx, rc);
			}
			else
				*vp = INT_TO_JSVAL(errno);
			break;
		case FILE_PROP_POSITION:
			if(p->fp) {
				rc=JS_SUSPENDREQUEST(cx);
				lng=ftell(p->fp);
				JS_RESUMEREQUEST(cx, rc);
				*vp=DOUBLE_TO_JSVAL((double)lng);
			}
			else
				*vp = INT_TO_JSVAL(-1);
			break;
		case FILE_PROP_LENGTH:
			rc=JS_SUSPENDREQUEST(cx);
			if(p->fp)	/* open? */
				lng = filelength(fileno(p->fp));
			else
				lng = flength(p->name);
			JS_RESUMEREQUEST(cx, rc);
			*vp=DOUBLE_TO_JSVAL((double)lng);
			break;
		case FILE_PROP_ATTRIBUTES:
			rc=JS_SUSPENDREQUEST(cx);
			in=getfattr(p->name);
			JS_RESUMEREQUEST(cx, rc);
			*vp=INT_TO_JSVAL(in);
			break;
		case FILE_PROP_DEBUG:
			*vp = BOOLEAN_TO_JSVAL(p->debug);
			break;
		case FILE_PROP_YENCODED:
			*vp = BOOLEAN_TO_JSVAL(p->yencoded);
			break;
		case FILE_PROP_UUENCODED:
			*vp = BOOLEAN_TO_JSVAL(p->uuencoded);
			break;
		case FILE_PROP_B64ENCODED:
			*vp = BOOLEAN_TO_JSVAL(p->b64encoded);
			break;
		case FILE_PROP_ROT13:
			*vp = BOOLEAN_TO_JSVAL(p->rot13);
			break;
		case FILE_PROP_NETWORK_ORDER:
			*vp = BOOLEAN_TO_JSVAL(p->network_byte_order);
			break;
		case FILE_PROP_DESCRIPTOR:
			if(p->fp)
				*vp = INT_TO_JSVAL(fileno(p->fp));
			else
				*vp = INT_TO_JSVAL(-1);
			break;
		case FILE_PROP_ETX:
			*vp = INT_TO_JSVAL(p->etx);
			break;
		case FILE_PROP_CHKSUM:
		case FILE_PROP_CRC16:
		case FILE_PROP_CRC32:
			*vp = JSVAL_ZERO;
			if(p->fp==NULL)
				break;
			/* fall-through */
		case FILE_PROP_MD5_HEX:
		case FILE_PROP_MD5_B64:
			*vp = JSVAL_VOID;
			if(p->fp==NULL)
				break;
			rc=JS_SUSPENDREQUEST(cx);
			offset=ftell(p->fp);			/* save current file position */
			fseek(p->fp,0,SEEK_SET);

			/* Initialization */
			switch(tiny) {
				case FILE_PROP_MD5_HEX:
				case FILE_PROP_MD5_B64:
					MD5_open(&md5_ctx);
					break;
			}

			/* calculate */
			while(!feof(p->fp)) {
				if((rd=fread(block,1,sizeof(block),p->fp))<1)
					break;
				switch(tiny) {
					case FILE_PROP_CHKSUM:
						for(i=0;i<rd;i++)
							sum+=block[i];
						break;
					case FILE_PROP_CRC16:
						for(i=0;i<rd;i++)
							c16=ucrc16(block[i],c16);
						break;
					case FILE_PROP_CRC32:
						for(i=0;i<rd;i++)
							c32=ucrc32(block[i],c32);
						break;
					case FILE_PROP_MD5_HEX:
					case FILE_PROP_MD5_B64:
						MD5_digest(&md5_ctx,block,rd);
						break;
					}
			}
			JS_RESUMEREQUEST(cx, rc);

			/* finalize */
			switch(tiny) {
				case FILE_PROP_CHKSUM:
					*vp=DOUBLE_TO_JSVAL((double)sum);
					break;
				case FILE_PROP_CRC16:
					*vp=UINT_TO_JSVAL(c16);
					break;
				case FILE_PROP_CRC32:
					*vp=UINT_TO_JSVAL(c32);
					break;
				case FILE_PROP_MD5_HEX:
				case FILE_PROP_MD5_B64:
					MD5_close(&md5_ctx,digest);
					if(tiny==FILE_PROP_MD5_HEX)
						MD5_hex((BYTE*)str,digest);
					else 
						b64_encode(str,sizeof(str)-1,(char *)digest,sizeof(digest));
					js_str=JS_NewStringCopyZ(cx, str);
					break;
			}
			rc=JS_SUSPENDREQUEST(cx);
			fseek(p->fp,offset,SEEK_SET);	/* restore saved file position */
			JS_RESUMEREQUEST(cx, rc);
			if(js_str!=NULL)
				*vp = STRING_TO_JSVAL(js_str);
			break;
	}

	return(JS_TRUE);
}

#define FILE_PROP_FLAGS JSPROP_ENUMERATE|JSPROP_READONLY

static jsSyncPropertySpec js_file_properties[] = {
/*		 name				,tinyid					,flags,				ver	*/
	{	"name"				,FILE_PROP_NAME			,FILE_PROP_FLAGS,	310},
	{	"mode"				,FILE_PROP_MODE			,FILE_PROP_FLAGS,	310},
	{	"exists"			,FILE_PROP_EXISTS		,FILE_PROP_FLAGS,	310},
	{	"is_open"			,FILE_PROP_IS_OPEN		,FILE_PROP_FLAGS,	310},
	{	"eof"				,FILE_PROP_EOF			,FILE_PROP_FLAGS,	310},
	{	"error"				,FILE_PROP_ERROR		,FILE_PROP_FLAGS,	310},
	{	"descriptor"		,FILE_PROP_DESCRIPTOR	,FILE_PROP_FLAGS,	310},
	/* writeable */
	{	"etx"				,FILE_PROP_ETX			,JSPROP_ENUMERATE,  310},
	{	"debug"				,FILE_PROP_DEBUG		,JSPROP_ENUMERATE,	310},
	{	"position"			,FILE_PROP_POSITION		,JSPROP_ENUMERATE,	310},
	{	"date"				,FILE_PROP_DATE			,JSPROP_ENUMERATE,	311},
	{	"length"			,FILE_PROP_LENGTH		,JSPROP_ENUMERATE,	310},
	{	"attributes"		,FILE_PROP_ATTRIBUTES	,JSPROP_ENUMERATE,	310},
	{	"network_byte_order",FILE_PROP_NETWORK_ORDER,JSPROP_ENUMERATE,	311},
	{	"rot13"				,FILE_PROP_ROT13		,JSPROP_ENUMERATE,	311},
	{	"uue"				,FILE_PROP_UUENCODED	,JSPROP_ENUMERATE,	311},
	{	"yenc"				,FILE_PROP_YENCODED		,JSPROP_ENUMERATE,	311},
	{	"base64"			,FILE_PROP_B64ENCODED	,JSPROP_ENUMERATE,	311},
	/* dynamically calculated */
	{	"crc16"				,FILE_PROP_CRC16		,FILE_PROP_FLAGS,	311},
	{	"crc32"				,FILE_PROP_CRC32		,FILE_PROP_FLAGS,	311},
	{	"chksum"			,FILE_PROP_CHKSUM		,FILE_PROP_FLAGS,	311},
	{	"md5_hex"			,FILE_PROP_MD5_HEX		,FILE_PROP_FLAGS,	311},
	{	"md5_base64"		,FILE_PROP_MD5_B64		,FILE_PROP_FLAGS,	311},
	{0}
};

#ifdef BUILD_JSDOCS
static char* file_prop_desc[] = {
	 "filename specified in constructor - <small>READ ONLY</small>"
	,"mode string specified in <i>open</i> call - <small>READ ONLY</small>"
	,"<i>true</i> if the file is open or exists (case-insensitive) - <small>READ ONLY</small>"
	,"<i>true</i> if the file has been opened successfully - <small>READ ONLY</small>"
	,"<i>true</i> if the current file position is at the <i>end of file</i> - <small>READ ONLY</small>"
	,"the last occurred error value (use clear_error to clear) - <small>READ ONLY</small>"
	,"the open file descriptor (advanced use only) - <small>READ ONLY</small>"
	,"end-of-text character (advanced use only), if non-zero used by <i>read</i>, <i>readln</i>, and <i>write</i>"
	,"set to <i>true</i> to enable debug log output"
	,"the current file position (offset in bytes), change value to seek within file"
	,"last modified date/time (in time_t format)"
	,"the current length of the file (in bytes)"
	,"file mode/attributes"
	,"set to <i>true</i> if binary data is to be written and read in Network Byte Order (big end first)"
	,"set to <i>true</i> to enable automatic ROT13 translatation of text"
	,"set to <i>true</i> to enable automatic Unix-to-Unix encode and decode on <tt>read</tt> and <tt>write</tt> calls"
	,"set to <i>true</i> to enable automatic yEnc encode and decode on <tt>read</tt> and <tt>write</tt> calls"
	,"set to <i>true</i> to enable automatic Base64 encode and decode on <tt>read</tt> and <tt>write</tt> calls"
	,"calculated 16-bit CRC of file contents - <small>READ ONLY</small>"
	,"calculated 32-bit CRC of file contents - <small>READ ONLY</small>"
	,"calculated 32-bit checksum of file contents - <small>READ ONLY</small>"
	,"calculated 128-bit MD5 digest of file contents as hexadecimal string - <small>READ ONLY</small>"
	,"calculated 128-bit MD5 digest of file contents as base64-encoded string - <small>READ ONLY</small>"
	,NULL
};
#endif


static jsSyncMethodSpec js_file_functions[] = {
	{"open",			js_open,			1,	JSTYPE_BOOLEAN,	JSDOCSTR("[mode=<tt>\"w+\"</tt>] [,shareable=<tt>false</tt>] [,buffer_length]")
	,JSDOCSTR("open file, <i>shareable</i> defaults to <i>false</i>, <i>buffer_length</i> defaults to 2048 bytes, "
		"mode (default: <tt>'w+'</tt>) specifies the type of access requested for the file, as follows:<br>"
		"<tt>r&nbsp</tt> open for reading; if the file does not exist or cannot be found, the open call fails<br>"
		"<tt>w&nbsp</tt> open an empty file for writing; if the given file exists, its contents are destroyed<br>"
		"<tt>a&nbsp</tt> open for writing at the end of the file (appending); creates the file first if it doesn�t exist<br>"
		"<tt>r+</tt> open for both reading and writing (the file must exist)<br>"
		"<tt>w+</tt> open an empty file for both reading and writing; if the given file exists, its contents are destroyed<br>"
		"<tt>a+</tt> open for reading and appending<br>"
		"<tt>b&nbsp</tt> open in binary (untranslated) mode; translations involving carriage-return and linefeed characters are suppressed (e.g. <tt>r+b</tt>)<br>"
		"<tt>e&nbsp</tt> open a <i>non-shareable</i> file (that must not already exist) for <i>exclusive</i> access <i>(introduced in v3.12)</i><br>"
		"<br><b>Note:</b> When using the <tt>iniSet</tt> methods to modify a <tt>.ini</tt> file, "
		"the file must be opened for both reading and writing.<br>"
		"<br><b>Note:</b> To open an existing or create a new file for both reading and writing "
		"(e.g. updating an <tt>.ini</tt> file) "
		"use the <i>exists</i> property like so:<br>"
		"<tt>file.open(file.exists ? 'r+':'w+');</tt>"
		"<br><b>Note:</b> When <i>shareable</i> is false, uses nopen() which will lock the file "
		"and perform automatic retries.  The lock mode is as follows:<br>"
		"<tt>r&nbsp</tt> DENYWRITE - Allows other scripts to open the file for reading, but not for writing.<br>"
		"<tt>w&nbsp</tt> DENYALL - Does not allow other scripts to open the file when <i>shareable</i> is set to true<br>"
		"<tt>a&nbsp</tt> DENYALL - Does not allow other scripts to open the file when <i>shareable</i> is set to true<br>"
		"<tt>r+</tt> DENYALL - Does not allow other scripts to open the file when <i>shareable</i> is set to true<br>"
		"<tt>w+</tt> DENYALL - Does not allow other scripts to open the file when <i>shareable</i> is set to true<br>"
		"<tt>a+</tt> DENYALL - Does not allow other scripts to open the file when <i>shareable</i> is set to true<br>"
		"When <i>shareable</i> is true uses fopen(), "
		"and will only attempt to open the file once and will perform no locking.  The behaviour "
		"when one script has a file opened with <i>shareable</i> set to a different value than is used "
		"with a new call is OS specific.  On Windows, the second open will always fail and on *nix, "
		"the second open will always succeed.<br>"
		)
	,310
	},		
	{"popen",			js_popen,			1,	JSTYPE_BOOLEAN,	JSDOCSTR("[mode=<tt>\"r+\"</tt>] [,buffer_length]")
	,JSDOCSTR("open pipe to command, <i>buffer_length</i> defaults to 2048 bytes, "
		"mode (default: <tt>'r+'</tt>) specifies the type of access requested for the file, as follows:<br>"
		"<tt>r&nbsp</tt> read the programs stdout;<br>"
		"<tt>w&nbsp</tt> write to the programs stdin<br>"
		"<tt>r+</tt> open for both reading stdout and writing stdin<br>"
		"(<b>only functional on UNIX systems</b>)"
		)
	,315
	},		
	{"close",			js_close,			0,	JSTYPE_VOID,	JSDOCSTR("")
	,JSDOCSTR("close file")
	,310
	},		
	{"remove",			js_delete,			0,	JSTYPE_BOOLEAN, JSDOCSTR("")
	,JSDOCSTR("remove the file from the disk")
	,310
	},
	{"clearError",		js_clear_error,		0,	JSTYPE_ALIAS },
	{"clear_error",		js_clear_error,		0,	JSTYPE_BOOLEAN, JSDOCSTR("")
	,JSDOCSTR("clears the current error value (AKA clearError)")
	,310
	},
	{"flush",			js_flush,			0,	JSTYPE_BOOLEAN,	JSDOCSTR("")
	,JSDOCSTR("flush/commit buffers to disk")
	,310
	},
	{"rewind",			js_rewind,			0,	JSTYPE_BOOLEAN,	JSDOCSTR("")
	,JSDOCSTR("repositions the file pointer (<i>position</i>) to the beginning of a file "
		"and clears error and end-of-file indicators")
	,311
	},
	{"truncate",		js_truncate,		0,	JSTYPE_BOOLEAN,	JSDOCSTR("[length=<tt>0</tt>]")
	,JSDOCSTR("changes the file <i>length</i> (default: 0) and repositions the file pointer "
	"(<i>position</i>) to the new end-of-file")
	,314
	},
	{"lock",			js_lock,			2,	JSTYPE_BOOLEAN,	JSDOCSTR("[offset=<tt>0</tt>] [,length=<i>file_length</i>-<i>offset</i>]")
	,JSDOCSTR("lock file record for exclusive access (file must be opened <i>shareable</i>)")
	,310
	},		
	{"unlock",			js_unlock,			2,	JSTYPE_BOOLEAN,	JSDOCSTR("[offset=<tt>0</tt>] [,length=<i>file_length</i>-<i>offset</i>]")
	,JSDOCSTR("unlock file record for exclusive access")
	,310
	},		
	{"read",			js_read,			0,	JSTYPE_STRING,	JSDOCSTR("[maxlen=<i>file_length</i>-<i>file_position</i>]")
	,JSDOCSTR("read a string from file (optionally unix-to-unix or base64 decoding in the process), "
		"<i>maxlen</i> defaults to the current length of the file minus the current file position")
	,310
	},
	{"readln",			js_readln,			0,	JSTYPE_STRING,	JSDOCSTR("[maxlen=<tt>512</tt>]")
	,JSDOCSTR("read a line-feed terminated string, <i>maxlen</i> defaults to 512 characters")
	,310
	},		
	{"readBin",			js_readbin,			0,	JSTYPE_NUMBER,	JSDOCSTR("[bytes=<tt>4</tt> [,count=<tt>1</tt>]")
	,JSDOCSTR("read one or more binary integers from the file, default number of <i>bytes</i> is 4 (32-bits). "
			  "if count is not equal to 1, an array is returned (even if no integers were read)")
	,310
	},
	{"readAll",			js_readall,			0,	JSTYPE_ARRAY,	JSDOCSTR("[maxlen=<tt>512</tt>]")
	,JSDOCSTR("read all lines into an array of strings, <i>maxlen</i> defaults to 512 characters")
	,310
	},
	{"write",			js_write,			1,	JSTYPE_BOOLEAN,	JSDOCSTR("text [,length=<i>text_length</i>]")
	,JSDOCSTR("write a string to the file (optionally unix-to-unix or base64 decoding in the process)")
	,310
	},
	{"writeln",			js_writeln,			0,	JSTYPE_BOOLEAN, JSDOCSTR("[text]")
	,JSDOCSTR("write a line-feed terminated string to the file")
	,310
	},
	{"writeBin",		js_writebin,		1,	JSTYPE_BOOLEAN,	JSDOCSTR("value(s) [,bytes=<tt>4</tt>]")
	,JSDOCSTR("write one or more binary integers to the file, default number of <i>bytes</i> is 4 (32-bits)."
			  "If value is an array, writes the entire array to the file.")
	,310
	},
	{"writeAll",		js_writeall,		0,	JSTYPE_BOOLEAN,	JSDOCSTR("array lines")
	,JSDOCSTR("write an array of strings to file")
	,310
	},		
	{"printf",			js_fprintf,			0,	JSTYPE_NUMBER,	JSDOCSTR("format [,args]")
	,JSDOCSTR("write a formatted string to the file (ala fprintf) - "
		"<small>CAUTION: for experienced C programmers ONLY</small>")
	,310
	},
	{"iniGetSections",	js_iniGetSections,	0,	JSTYPE_ARRAY,	JSDOCSTR("[prefix=<i>none</i>]")
	,JSDOCSTR("parse all section names from a <tt>.ini</tt> file (format = '<tt>[section]</tt>') "
		"and return the section names as an <i>array of strings</i>, "
		"optionally, only those section names that begin with the specified <i>prefix</i>")
	,311
	},
	{"iniGetKeys",		js_iniGetKeys,		1,	JSTYPE_ARRAY,	JSDOCSTR("[section=<i>root</i>]")
	,JSDOCSTR("parse all key names from the specified <i>section</i> in a <tt>.ini</tt> file "
		"and return the key names as an <i>array of strings</i>. "
		"if <i>section</i> is undefined, returns key names from the <i>root</i> section")
	,311
	},
	{"iniGetValue",		js_iniGetValue,		3,	JSTYPE_UNDEF,	JSDOCSTR("section, key [,default=<i>none</i>]")
	,JSDOCSTR("parse a key from a <tt>.ini</tt> file and return its value (format = '<tt>key = value</tt>'). "
		"returns the specified <i>default</i> value if the key or value is missing or invalid. "
		"to parse a key from the <i>root</i> section, pass <i>null</i> for <i>section</i>. "
		"will return a <i>bool</i>, <i>number</i>, <i>string</i>, or an <i>array of strings</i> "
		"determined by the type of <i>default</i> value specified")
	,311
	},
	{"iniSetValue",		js_iniSetValue,		3,	JSTYPE_BOOLEAN,	JSDOCSTR("section, key, [value=<i>none</i>]")
	,JSDOCSTR("set the specified <i>key</i> to the specified <i>value</i> in the specified <i>section</i> "
		"of a <tt>.ini</tt> file. "
		"to set a key in the <i>root</i> section, pass <i>null</i> for <i>section</i>. ")
	,312
	},
	{"iniGetObject",	js_iniGetObject,	1,	JSTYPE_OBJECT,	JSDOCSTR("[section=<i>root</i>]")
	,JSDOCSTR("parse an entire section from a .ini file "
		"and return all of its keys and values as properties of an object. "
		"if <i>section</i> is undefined, returns key and values from the <i>root</i> section")
	,311
	},
	{"iniSetObject",	js_iniSetObject,	2,	JSTYPE_BOOLEAN,	JSDOCSTR("section, object")
	,JSDOCSTR("write all the properties of the specified <i>object</i> as separate <tt>key=value</tt> pairs "
		"in the specified <i>section</i> of a <tt>.ini</tt> file. "
		"to write an object in the <i>root</i> section, pass <i>null</i> for <i>section</i>. ")
	,312
	},
	{"iniGetAllObjects",js_iniGetAllObjects,1,	JSTYPE_ARRAY,	JSDOCSTR("[name_property] [,prefix=<i>none</i>]")
	,JSDOCSTR("parse all sections from a .ini file and return all (non-<i>root</i>) sections "
		"in an array of objects with each section's keys as properties of each object. "
		"<i>name_property</i> is the name of the property to create to contain the section's name "
		"(default is <tt>\"name\"</tt>), "
		"the optional <i>prefix</i> has the same use as in the <tt>iniGetSections</tt> method, "
		"if a <i>prefix</i> is specified, it is removed from each section's name" )
	,311
	},
	{"iniSetAllObjects",js_iniSetAllObjects,1,	JSTYPE_ARRAY,	JSDOCSTR("object array [,name_property=<tt>\"name\"</tt>]")
	,JSDOCSTR("write an array of objects to a .ini file, each object in its own section named "
	"after the object's <i>name_property</i> (default: <tt>name</tt>)")
	,312
	},
	{"iniRemoveKey",	js_iniRemoveKey,	2,	JSTYPE_BOOLEAN,	JSDOCSTR("section, key")
	,JSDOCSTR("remove specified <i>key</i> from specified <i>section</i> in <tt>.ini</tt> file.")
	,314
	},
	{"iniRemoveSection",js_iniRemoveSection,1,	JSTYPE_BOOLEAN,	JSDOCSTR("section")
	,JSDOCSTR("remove specified <i>section</i> from <tt>.ini</tt> file.")
	,314
	},
	{0}
};

/* File Destructor */

static void js_finalize_file(JSContext *cx, JSObject *obj)
{
	private_t* p;
	
	if((p=(private_t*)JS_GetPrivate(cx,obj))==NULL)
		return;

	if(p->external==JS_FALSE && p->fp!=NULL)
		fclose(p->fp);

	dbprintf(FALSE, p, "closed: %s",p->name);

	free(p);

	JS_SetPrivate(cx, obj, NULL);
}

static JSBool js_file_resolve(JSContext *cx, JSObject *obj, jsid id)
{
	char*			name=NULL;
	JSBool			ret;

	if(id != JSID_VOID && id != JSID_EMPTY) {
		jsval idval;
		
		JS_IdToValue(cx, id, &idval);
		if(JSVAL_IS_STRING(idval))
			JSSTRING_TO_MSTRING(cx, JSVAL_TO_STRING(idval), name, NULL);
	}

	ret=js_SyncResolve(cx, obj, name, js_file_properties, js_file_functions, NULL, 0);
	if(name)
		free(name);
	return ret;
}

static JSBool js_file_enumerate(JSContext *cx, JSObject *obj)
{
	return(js_file_resolve(cx, obj, JSID_VOID));
}

static JSClass js_file_class = {
     "File"					/* name			*/
    ,JSCLASS_HAS_PRIVATE	/* flags		*/
	,JS_PropertyStub		/* addProperty	*/
	,JS_PropertyStub		/* delProperty	*/
	,js_file_get			/* getProperty	*/
	,js_file_set			/* setProperty	*/
	,js_file_enumerate		/* enumerate	*/
	,js_file_resolve		/* resolve		*/
	,JS_ConvertStub			/* convert		*/
	,js_finalize_file		/* finalize		*/
};

/* File Constructor (creates file descriptor) */

static JSBool
js_file_constructor(JSContext *cx, uintN argc, jsval *arglist)
{
	JSObject *obj=JS_THIS_OBJECT(cx, arglist);
	jsval *argv=JS_ARGV(cx, arglist);
	JSString*	str;
	private_t*	p;

	obj=JS_NewObject(cx, &js_file_class, NULL, NULL);
	JS_SET_RVAL(cx, arglist, OBJECT_TO_JSVAL(obj));
	if((str = JS_ValueToString(cx, argv[0]))==NULL) {
		JS_ReportError(cx,"No filename specified");
		return(JS_FALSE);
	}

	if((p=(private_t*)calloc(1,sizeof(private_t)))==NULL) {
		JS_ReportError(cx,"calloc failed");
		return(JS_FALSE);
	}

	JSSTRING_TO_STRBUF(cx, str, p->name, sizeof(p->name), NULL);

	if(!JS_SetPrivate(cx, obj, p)) {
		dbprintf(TRUE, p, "JS_SetPrivate failed");
		return(JS_FALSE);
	}

#ifdef BUILD_JSDOCS
	js_DescribeSyncObject(cx,obj,"Class used for opening, creating, reading, or writing files on the local file system<p>"
		"Special features include:</h2><ol type=disc>"
			"<li>Exclusive-access files (default) or shared files<ol type=circle>"
				"<li>optional record-locking"
				"<li>buffered or non-buffered I/O"
				"</ol>"
			"<li>Support for binary files<ol type=circle>"
				"<li>native or network byte order (endian)"
				"<li>automatic Unix-to-Unix (<i>UUE</i>), yEncode (<i>yEnc</i>) or Base64 encoding/decoding"
				"</ol>"
			"<li>Support for ASCII text files<ol type=circle>"
				"<li>supports line-based I/O<ol type=square>"
					"<li>entire file may be read or written as an array of strings"
					"<li>individual lines may be read or written one line at a time"
					"</ol>"
				"<li>supports fixed-length records<ol type=square>"
					"<li>optional end-of-text (<i>etx</i>) character for automatic record padding/termination"
					"<li>Synchronet <tt>.dat</tt> files use an <i>etx</i> value of 3 (Ctrl-C)"
					"</ol>"
				"<li>supports <tt>.ini</tt> formated configuration files<ol type=square>"
					"<li>concept and support of <i>root</i> ini sections added in v3.12"
					"</ol>"
				"<li>optional ROT13 encoding/translation"
				"</ol>"
			"<li>Dynamically-calculated industry standard checksums (e.g. CRC-16, CRC-32, MD5)"
			"</ol>"
			,310
			);
	js_DescribeSyncConstructor(cx,obj,"To create a new File object: <tt>var f = new File(<i>filename</i>)</tt>");
	js_CreateArrayOfStrings(cx, obj, "_property_desc_list", file_prop_desc, JSPROP_READONLY);
#endif

	dbprintf(FALSE, p, "object constructed");
	return(JS_TRUE);
}

JSObject* DLLCALL js_CreateFileClass(JSContext* cx, JSObject* parent)
{
	JSObject*	obj;

	obj = JS_InitClass(cx, parent, NULL
		,&js_file_class
		,js_file_constructor
		,1		/* number of constructor args */
		,NULL	/* props, set in constructor */
		,NULL	/* funcs, set in constructor */
		,NULL,NULL);

	return(obj);
}

JSObject* DLLCALL js_CreateFileObject(JSContext* cx, JSObject* parent, char *name, FILE* fp)
{
	JSObject* obj;
	private_t*	p;

	obj = JS_DefineObject(cx, parent, name, &js_file_class, NULL
		,JSPROP_ENUMERATE|JSPROP_READONLY);

	if(obj==NULL)
		return(NULL);

	if((p=(private_t*)calloc(1,sizeof(private_t)))==NULL)
		return(NULL);

	p->fp=fp;
	p->debug=JS_FALSE;
	p->external=JS_TRUE;

	if(!JS_SetPrivate(cx, obj, p)) {
		dbprintf(TRUE, p, "JS_SetPrivate failed");
		return(NULL);
	}

	dbprintf(FALSE, p, "object created");

	return(obj);
}


#endif	/* JAVSCRIPT */
