/**
 * @author Alberto DEMICHELIS
 * @author Edouard DUPIN
 * @copyright 2018, Edouard DUPIN, all right reserved
 * @copyright 2003-2017, Alberto DEMICHELIS, all right reserved
 * @license MPL-2 (see license file)
 */

#include <rabbit/rabbit.hpp>
#include <rabbit-std/sqstdstring.hpp>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>

#define MAX_FORMAT_LEN  20
#define MAX_WFORMAT_LEN 3
#define ADDITIONAL_FORMAT_SPACE (100*sizeof(char))

static rabbit::Bool isfmtchr(char ch)
{
	switch(ch) {
		case '-':
		case '+':
		case ' ':
		case '#':
		case '0':
			return SQTrue;
	}
	return SQFalse;
}

static int64_t validate_format(rabbit::VirtualMachine* v, char *fmt, const char *src, int64_t n,int64_t &width)
{
	char *dummy;
	char swidth[MAX_WFORMAT_LEN];
	int64_t wc = 0;
	int64_t start = n;
	fmt[0] = '%';
	while (isfmtchr(src[n])) n++;
	while (isdigit(src[n])) {
		swidth[wc] = src[n];
		n++;
		wc++;
		if(wc>=MAX_WFORMAT_LEN)
			return sq_throwerror(v,"width format too long");
	}
	swidth[wc] = '\0';
	if(wc > 0) {
		width = strtol(swidth,&dummy,10);
	}
	else
		width = 0;
	if (src[n] == '.') {
		n++;

		wc = 0;
		while (isdigit(src[n])) {
			swidth[wc] = src[n];
			n++;
			wc++;
			if(wc>=MAX_WFORMAT_LEN)
				return sq_throwerror(v,"precision format too long");
		}
		swidth[wc] = '\0';
		if(wc > 0) {
			width += strtol(swidth,&dummy,10);

		}
	}
	if (n-start > MAX_FORMAT_LEN )
		return sq_throwerror(v,"format too long");
	memcpy(&fmt[1],&src[start],((n-start)+1)*sizeof(char));
	fmt[(n-start)+2] = '\0';
	return n;
}

rabbit::Result rabbit::std::format(rabbit::VirtualMachine* v,int64_t nformatstringidx,int64_t *outlen,char **output)
{
	const char *format;
	char *dest;
	char fmt[MAX_FORMAT_LEN];
	const rabbit::Result res = sq_getstring(v,nformatstringidx,&format);
	if (SQ_FAILED(res)) {
		return res; // propagate the error
	}
	int64_t format_size = sq_getsize(v,nformatstringidx);
	int64_t allocated = (format_size+2)*sizeof(char);
	dest = sq_getscratchpad(v,allocated);
	int64_t n = 0,i = 0, nparam = nformatstringidx+1, w = 0;
	//while(format[n] != '\0')
	while(n < format_size)
	{
		if(format[n] != '%') {
			assert(i < allocated);
			dest[i++] = format[n];
			n++;
		}
		else if(format[n+1] == '%') { //handles %%
				dest[i++] = '%';
				n += 2;
		}
		else {
			n++;
			if( nparam > sq_gettop(v) )
				return sq_throwerror(v,"not enough parameters for the given format string");
			n = validate_format(v,fmt,format,n,w);
			if(n < 0) return -1;
			int64_t addlen = 0;
			int64_t valtype = 0;
			const char *ts = NULL;
			int64_t ti = 0;
			float_t tf = 0;
			switch(format[n]) {
				case 's':
					if(SQ_FAILED(sq_getstring(v,nparam,&ts)))
						return sq_throwerror(v,"string expected for the specified format");
					addlen = (sq_getsize(v,nparam)*sizeof(char))+((w+1)*sizeof(char));
					valtype = 's';
					break;
				case 'i':
				case 'd':
				case 'o':
				case 'u':
				case 'x':
				case 'X':
					{
					size_t flen = strlen(fmt);
					int64_t fpos = flen - 1;
					char f = fmt[fpos];
					const char *prec = (const char *)_PRINT_INT_PREC;
					while(*prec != '\0') {
						fmt[fpos++] = *prec++;
					}
					fmt[fpos++] = f;
					fmt[fpos++] = '\0';
					}
				case 'c':
					if(SQ_FAILED(sq_getinteger(v,nparam,&ti)))
						return sq_throwerror(v,"integer expected for the specified format");
					addlen = (ADDITIONAL_FORMAT_SPACE)+((w+1)*sizeof(char));
					valtype = 'i';
					break;
				case 'f': case 'g': case 'G': case 'e':  case 'E':
					if(SQ_FAILED(sq_getfloat(v,nparam,&tf)))
						return sq_throwerror(v,"float expected for the specified format");
					addlen = (ADDITIONAL_FORMAT_SPACE)+((w+1)*sizeof(char));
					valtype = 'f';
					break;
				default:
					return sq_throwerror(v,"invalid format");
			}
			n++;
			allocated += addlen + sizeof(char);
			dest = sq_getscratchpad(v,allocated);
			switch(valtype) {
			case 's': i += snprintf(&dest[i],allocated,fmt,ts); break;
			case 'i': i += snprintf(&dest[i],allocated,fmt,ti); break;
			case 'f': i += snprintf(&dest[i],allocated,fmt,tf); break;
			};
			nparam ++;
		}
	}
	*outlen = i;
	dest[i] = '\0';
	*output = dest;
	return SQ_OK;
}

static int64_t _string_printf(rabbit::VirtualMachine* v)
{
	char *dest = NULL;
	int64_t length = 0;
	if(SQ_FAILED(rabbit::std::format(v,2,&length,&dest)))
		return -1;

	SQPRINTFUNCTION printfunc = sq_getprintfunc(v);
	if(printfunc) printfunc(v,dest);

	return 0;
}

static int64_t _string_format(rabbit::VirtualMachine* v)
{
	char *dest = NULL;
	int64_t length = 0;
	if(SQ_FAILED(rabbit::std::format(v,2,&length,&dest)))
		return -1;
	sq_pushstring(v,dest,length);
	return 1;
}

static void __strip_l(const char *str,const char **start)
{
	const char *t = str;
	while(((*t) != '\0') && isspace(*t)){ t++; }
	*start = t;
}

static void __strip_r(const char *str,int64_t len,const char **end)
{
	if(len == 0) {
		*end = str;
		return;
	}
	const char *t = &str[len-1];
	while(t >= str && isspace(*t)) { t--; }
	*end = t + 1;
}

static int64_t _string_strip(rabbit::VirtualMachine* v)
{
	const char *str,*start,*end;
	sq_getstring(v,2,&str);
	int64_t len = sq_getsize(v,2);
	__strip_l(str,&start);
	__strip_r(str,len,&end);
	sq_pushstring(v,start,end - start);
	return 1;
}

static int64_t _string_lstrip(rabbit::VirtualMachine* v)
{
	const char *str,*start;
	sq_getstring(v,2,&str);
	__strip_l(str,&start);
	sq_pushstring(v,start,-1);
	return 1;
}

static int64_t _string_rstrip(rabbit::VirtualMachine* v)
{
	const char *str,*end;
	sq_getstring(v,2,&str);
	int64_t len = sq_getsize(v,2);
	__strip_r(str,len,&end);
	sq_pushstring(v,str,end - str);
	return 1;
}

static int64_t _string_split(rabbit::VirtualMachine* v)
{
	const char *str,*seps;
	char *stemp;
	sq_getstring(v,2,&str);
	sq_getstring(v,3,&seps);
	int64_t sepsize = sq_getsize(v,3);
	if(sepsize == 0) return sq_throwerror(v,"empty separators string");
	int64_t memsize = (sq_getsize(v,2)+1)*sizeof(char);
	stemp = sq_getscratchpad(v,memsize);
	memcpy(stemp,str,memsize);
	char *start = stemp;
	char *end = stemp;
	sq_newarray(v,0);
	while(*end != '\0')
	{
		char cur = *end;
		for(int64_t i = 0; i < sepsize; i++)
		{
			if(cur == seps[i])
			{
				*end = 0;
				sq_pushstring(v,start,-1);
				sq_arrayappend(v,-2);
				start = end + 1;
				break;
			}
		}
		end++;
	}
	if(end != start)
	{
		sq_pushstring(v,start,-1);
		sq_arrayappend(v,-2);
	}
	return 1;
}

static int64_t _string_escape(rabbit::VirtualMachine* v)
{
	const char *str;
	char *dest,*resstr;
	int64_t size;
	sq_getstring(v,2,&str);
	size = sq_getsize(v,2);
	if(size == 0) {
		sq_push(v,2);
		return 1;
	}
	const char *escpat = "\\x%02x";
	const int64_t maxescsize = 4;
	int64_t destcharsize = (size * maxescsize); //assumes every char could be escaped
	resstr = dest = (char *)sq_getscratchpad(v,destcharsize * sizeof(char));
	char c;
	char escch;
	int64_t escaped = 0;
	for(int n = 0; n < size; n++){
		c = *str++;
		escch = 0;
		if(isprint(c) || c == 0) {
			switch(c) {
			case '\a': escch = 'a'; break;
			case '\b': escch = 'b'; break;
			case '\t': escch = 't'; break;
			case '\n': escch = 'n'; break;
			case '\v': escch = 'v'; break;
			case '\f': escch = 'f'; break;
			case '\r': escch = 'r'; break;
			case '\\': escch = '\\'; break;
			case '\"': escch = '\"'; break;
			case '\'': escch = '\''; break;
			case 0: escch = '0'; break;
			}
			if(escch) {
				*dest++ = '\\';
				*dest++ = escch;
				escaped++;
			}
			else {
				*dest++ = c;
			}
		}
		else {

			dest += snprintf(dest, destcharsize, escpat, c);
			escaped++;
		}
	}

	if(escaped) {
		sq_pushstring(v,resstr,dest - resstr);
	}
	else {
		sq_push(v,2); //nothing escaped
	}
	return 1;
}

static int64_t _string_startswith(rabbit::VirtualMachine* v)
{
	const char *str,*cmp;
	sq_getstring(v,2,&str);
	sq_getstring(v,3,&cmp);
	int64_t len = sq_getsize(v,2);
	int64_t cmplen = sq_getsize(v,3);
	rabbit::Bool ret = SQFalse;
	if(cmplen <= len) {
		ret = memcmp(str,cmp,sq_rsl(cmplen)) == 0 ? SQTrue : SQFalse;
	}
	sq_pushbool(v,ret);
	return 1;
}

static int64_t _string_endswith(rabbit::VirtualMachine* v)
{
	const char *str,*cmp;
	sq_getstring(v,2,&str);
	sq_getstring(v,3,&cmp);
	int64_t len = sq_getsize(v,2);
	int64_t cmplen = sq_getsize(v,3);
	rabbit::Bool ret = SQFalse;
	if(cmplen <= len) {
		ret = memcmp(&str[len - cmplen],cmp,sq_rsl(cmplen)) == 0 ? SQTrue : SQFalse;
	}
	sq_pushbool(v,ret);
	return 1;
}

#define SETUP_REX(v) \
	rabbit::std::SQRex *self = NULL; \
	rabbit::sq_getinstanceup(v,1,(rabbit::UserPointer *)&self,0);

static int64_t _rexobj_releasehook(rabbit::UserPointer p, int64_t SQ_UNUSED_ARG(size))
{
	rabbit::std::SQRex *self = ((rabbit::std::SQRex *)p);
	rabbit::std::rex_free(self);
	return 1;
}

static int64_t _regexp_match(rabbit::VirtualMachine* v)
{
	SETUP_REX(v);
	const char *str;
	sq_getstring(v,2,&str);
	if(rabbit::std::rex_match(self,str) == SQTrue)
	{
		sq_pushbool(v,SQTrue);
		return 1;
	}
	sq_pushbool(v,SQFalse);
	return 1;
}

static void _addrexmatch(rabbit::VirtualMachine* v,const char *str,const char *begin,const char *end)
{
	sq_newtable(v);
	sq_pushstring(v,"begin",-1);
	sq_pushinteger(v,begin - str);
	sq_rawset(v,-3);
	sq_pushstring(v,"end",-1);
	sq_pushinteger(v,end - str);
	sq_rawset(v,-3);
}

static int64_t _regexp_search(rabbit::VirtualMachine* v)
{
	SETUP_REX(v);
	const char *str,*begin,*end;
	int64_t start = 0;
	sq_getstring(v,2,&str);
	if(sq_gettop(v) > 2) sq_getinteger(v,3,&start);
	if(rabbit::std::rex_search(self,str+start,&begin,&end) == SQTrue) {
		_addrexmatch(v,str,begin,end);
		return 1;
	}
	return 0;
}

static int64_t _regexp_capture(rabbit::VirtualMachine* v)
{
	SETUP_REX(v);
	const char *str,*begin,*end;
	int64_t start = 0;
	sq_getstring(v,2,&str);
	if(sq_gettop(v) > 2) sq_getinteger(v,3,&start);
	if(rabbit::std::rex_search(self,str+start,&begin,&end) == SQTrue) {
		int64_t n = rabbit::std::rex_getsubexpcount(self);
		rabbit::std::SQRexMatch match;
		sq_newarray(v,0);
		for(int64_t i = 0;i < n; i++) {
			rabbit::std::rex_getsubexp(self,i,&match);
			if(match.len > 0)
				_addrexmatch(v,str,match.begin,match.begin+match.len);
			else
				_addrexmatch(v,str,str,str); //empty match
			sq_arrayappend(v,-2);
		}
		return 1;
	}
	return 0;
}

static int64_t _regexp_subexpcount(rabbit::VirtualMachine* v)
{
	SETUP_REX(v);
	sq_pushinteger(v,rabbit::std::rex_getsubexpcount(self));
	return 1;
}

static int64_t _regexp_constructor(rabbit::VirtualMachine* v)
{
	const char *error,*pattern;
	sq_getstring(v,2,&pattern);
	rabbit::std::SQRex *rex = rabbit::std::rex_compile(pattern,&error);
	if(!rex) return sq_throwerror(v,error);
	sq_setinstanceup(v,1,rex);
	sq_setreleasehook(v,1,_rexobj_releasehook);
	return 0;
}

static int64_t _regexp__typeof(rabbit::VirtualMachine* v)
{
	sq_pushstring(v,"regexp",-1);
	return 1;
}

#define _DECL_REX_FUNC(name,nparams,pmask) {#name,_regexp_##name,nparams,pmask}
static const rabbit::RegFunction rexobj_funcs[]={
	_DECL_REX_FUNC(constructor,2,".s"),
	_DECL_REX_FUNC(search,-2,"xsn"),
	_DECL_REX_FUNC(match,2,"xs"),
	_DECL_REX_FUNC(capture,-2,"xsn"),
	_DECL_REX_FUNC(subexpcount,1,"x"),
	_DECL_REX_FUNC(_typeof,1,"x"),
	{NULL,(SQFUNCTION)0,0,NULL}
};
#undef _DECL_REX_FUNC

#define _DECL_FUNC(name,nparams,pmask) {#name,_string_##name,nparams,pmask}
static const rabbit::RegFunction stringlib_funcs[]={
	_DECL_FUNC(format,-2,".s"),
	_DECL_FUNC(printf,-2,".s"),
	_DECL_FUNC(strip,2,".s"),
	_DECL_FUNC(lstrip,2,".s"),
	_DECL_FUNC(rstrip,2,".s"),
	_DECL_FUNC(split,3,".ss"),
	_DECL_FUNC(escape,2,".s"),
	_DECL_FUNC(startswith,3,".ss"),
	_DECL_FUNC(endswith,3,".ss"),
	{NULL,(SQFUNCTION)0,0,NULL}
};
#undef _DECL_FUNC


int64_t rabbit::std::register_stringlib(rabbit::VirtualMachine* v)
{
	sq_pushstring(v,"regexp",-1);
	sq_newclass(v,SQFalse);
	int64_t i = 0;
	while(rexobj_funcs[i].name != 0) {
		const rabbit::RegFunction &f = rexobj_funcs[i];
		sq_pushstring(v,f.name,-1);
		sq_newclosure(v,f.f,0);
		sq_setparamscheck(v,f.nparamscheck,f.typemask);
		sq_setnativeclosurename(v,-1,f.name);
		sq_newslot(v,-3,SQFalse);
		i++;
	}
	sq_newslot(v,-3,SQFalse);

	i = 0;
	while(stringlib_funcs[i].name!=0)
	{
		sq_pushstring(v,stringlib_funcs[i].name,-1);
		sq_newclosure(v,stringlib_funcs[i].f,0);
		sq_setparamscheck(v,stringlib_funcs[i].nparamscheck,stringlib_funcs[i].typemask);
		sq_setnativeclosurename(v,-1,stringlib_funcs[i].name);
		sq_newslot(v,-3,SQFalse);
		i++;
	}
	return 1;
}
