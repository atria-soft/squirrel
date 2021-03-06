/**
 * @author Alberto DEMICHELIS
 * @author Edouard DUPIN
 * @copyright 2018, Edouard DUPIN, all right reserved
 * @copyright 2003-2017, Alberto DEMICHELIS, all right reserved
 * @license MPL-2 (see license file)
 */

#include <rabbit/rabbit.hpp>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <rabbit-std/sqstdstring.hpp>

#ifdef _DEBUG
#include <stdio.h>

static const char *g_nnames[] = {
	"NONE",
	"OP_GREEDY",
	"OP_OR",
	"OP_EXPR",
	"OP_NOCAPEXPR",
	"OP_DOT",
	"OP_CLASS",
	"OP_CCLASS",
	"OP_NCLASS",
	"OP_RANGE",
	"OP_CHAR",
	"OP_EOL",
	"OP_BOL",
	"OP_WB",
	"OP_MB"
};

#endif

#define OP_GREEDY	   (UINT8_MAX+1) // * + ? {n}
#define OP_OR		   (UINT8_MAX+2)
#define OP_EXPR		 (UINT8_MAX+3) //parentesis ()
#define OP_NOCAPEXPR	(UINT8_MAX+4) //parentesis (?:)
#define OP_DOT		  (UINT8_MAX+5)
#define OP_CLASS		(UINT8_MAX+6)
#define OP_CCLASS	   (UINT8_MAX+7)
#define OP_NCLASS	   (UINT8_MAX+8) //negates class the [^
#define OP_RANGE		(UINT8_MAX+9)
#define OP_CHAR		 (UINT8_MAX+10)
#define OP_EOL		  (UINT8_MAX+11)
#define OP_BOL		  (UINT8_MAX+12)
#define OP_WB		   (UINT8_MAX+13)
#define OP_MB		   (UINT8_MAX+14) //match balanced

#define SQREX_SYMBOL_ANY_CHAR ('.')
#define SQREX_SYMBOL_GREEDY_ONE_OR_MORE ('+')
#define SQREX_SYMBOL_GREEDY_ZERO_OR_MORE ('*')
#define SQREX_SYMBOL_GREEDY_ZERO_OR_ONE ('?')
#define SQREX_SYMBOL_BRANCH ('|')
#define SQREX_SYMBOL_END_OF_STRING ('$')
#define SQREX_SYMBOL_BEGINNING_OF_STRING ('^')
#define SQREX_SYMBOL_ESCAPE_CHAR ('\\')

namespace rabbit {

namespace std {

		typedef int SQRexNodeType;
		
		typedef struct tagSQRexNode{
			SQRexNodeType type;
			int64_t left;
			int64_t right;
			int64_t next;
		}SQRexNode;
		
		struct SQRex{
			const char *_eol;
			const char *_bol;
			const char *_p;
			int64_t _first;
			int64_t _op;
			SQRexNode *_nodes;
			int64_t _nallocated;
			int64_t _nsize;
			int64_t _nsubexpr;
			rabbit::std::SQRexMatch *_matches;
			int64_t _currsubexp;
			void *_jmpbuf;
			const char **_error;
		};
		
		static int64_t rex_list(SQRex *exp);
		
		static int64_t rex_newnode(SQRex *exp, SQRexNodeType type)
		{
			SQRexNode n;
			n.type = type;
			n.next = n.right = n.left = -1;
			if(type == OP_EXPR)
				n.right = exp->_nsubexpr++;
			if(exp->_nallocated < (exp->_nsize + 1)) {
				int64_t oldsize = exp->_nallocated;
				exp->_nallocated *= 2;
				exp->_nodes = (SQRexNode *)sq_realloc(exp->_nodes, oldsize * sizeof(SQRexNode) ,exp->_nallocated * sizeof(SQRexNode));
			}
			exp->_nodes[exp->_nsize++] = n;
			int64_t newid = exp->_nsize - 1;
			return (int64_t)newid;
		}
		
		static void rex_error(SQRex *exp,const char *error)
		{
			if(exp->_error) *exp->_error = error;
			longjmp(*((jmp_buf*)exp->_jmpbuf),-1);
		}
		
		static void rex_expect(SQRex *exp, int64_t n){
			if((*exp->_p) != n)
				rabbit::std::rex_error(exp, "expected paren");
			exp->_p++;
		}
		
		static char rex_escapechar(SQRex *exp)
		{
			if(*exp->_p == SQREX_SYMBOL_ESCAPE_CHAR){
				exp->_p++;
				switch(*exp->_p) {
				case 'v': exp->_p++; return '\v';
				case 'n': exp->_p++; return '\n';
				case 't': exp->_p++; return '\t';
				case 'r': exp->_p++; return '\r';
				case 'f': exp->_p++; return '\f';
				default: return (*exp->_p++);
				}
			} else if(!isprint(*exp->_p)) rabbit::std::rex_error(exp,"letter expected");
			return (*exp->_p++);
		}
		
		static int64_t rex_charclass(SQRex *exp,int64_t classid)
		{
			int64_t n = rabbit::std::rex_newnode(exp,OP_CCLASS);
			exp->_nodes[n].left = classid;
			return n;
		}
		
		static int64_t rex_charnode(SQRex *exp,rabbit::Bool isclass)
		{
			char t;
			if(*exp->_p == SQREX_SYMBOL_ESCAPE_CHAR) {
				exp->_p++;
				switch(*exp->_p) {
					case 'n': exp->_p++; return rabbit::std::rex_newnode(exp,'\n');
					case 't': exp->_p++; return rabbit::std::rex_newnode(exp,'\t');
					case 'r': exp->_p++; return rabbit::std::rex_newnode(exp,'\r');
					case 'f': exp->_p++; return rabbit::std::rex_newnode(exp,'\f');
					case 'v': exp->_p++; return rabbit::std::rex_newnode(exp,'\v');
					case 'a': case 'A': case 'w': case 'W': case 's': case 'S':
					case 'd': case 'D': case 'x': case 'X': case 'c': case 'C':
					case 'p': case 'P': case 'l': case 'u':
						{
						t = *exp->_p; exp->_p++;
						return rabbit::std::rex_charclass(exp,t);
						}
					case 'm':
						{
							 char cb, ce; //cb = character begin match ce = character end match
							 cb = *++exp->_p; //skip 'm'
							 ce = *++exp->_p;
							 exp->_p++; //points to the next char to be parsed
							 if ((!cb) || (!ce)) rabbit::std::rex_error(exp,"balanced chars expected");
							 if ( cb == ce ) rabbit::std::rex_error(exp,"open/close char can't be the same");
							 int64_t node =  rabbit::std::rex_newnode(exp,OP_MB);
							 exp->_nodes[node].left = cb;
							 exp->_nodes[node].right = ce;
							 return node;
						}
					case 0:
						rabbit::std::rex_error(exp,"letter expected for argument of escape sequence");
						break;
					case 'b':
					case 'B':
						if(!isclass) {
							int64_t node = rabbit::std::rex_newnode(exp,OP_WB);
							exp->_nodes[node].left = *exp->_p;
							exp->_p++;
							return node;
						} //else default
					default:
						t = *exp->_p; exp->_p++;
						return rabbit::std::rex_newnode(exp,t);
				}
			}
			else if(!isprint(*exp->_p)) {
		
				rabbit::std::rex_error(exp,"letter expected");
			}
			t = *exp->_p; exp->_p++;
			return rabbit::std::rex_newnode(exp,t);
		}
		static int64_t rex_class(SQRex *exp)
		{
			int64_t ret = -1;
			int64_t first = -1,chain;
			if(*exp->_p == SQREX_SYMBOL_BEGINNING_OF_STRING){
				ret = rabbit::std::rex_newnode(exp,OP_NCLASS);
				exp->_p++;
			}else ret = rabbit::std::rex_newnode(exp,OP_CLASS);
		
			if(*exp->_p == ']') rabbit::std::rex_error(exp,"empty class");
			chain = ret;
			while(*exp->_p != ']' && exp->_p != exp->_eol) {
				if(*exp->_p == '-' && first != -1){
					int64_t r;
					if(*exp->_p++ == ']') rabbit::std::rex_error(exp,"unfinished range");
					r = rabbit::std::rex_newnode(exp,OP_RANGE);
					if(exp->_nodes[first].type>*exp->_p) rabbit::std::rex_error(exp,"invalid range");
					if(exp->_nodes[first].type == OP_CCLASS) rabbit::std::rex_error(exp,"cannot use character classes in ranges");
					exp->_nodes[r].left = exp->_nodes[first].type;
					int64_t t = rabbit::std::rex_escapechar(exp);
					exp->_nodes[r].right = t;
					exp->_nodes[chain].next = r;
					chain = r;
					first = -1;
				}
				else{
					if(first!=-1){
						int64_t c = first;
						exp->_nodes[chain].next = c;
						chain = c;
						first = rabbit::std::rex_charnode(exp,SQTrue);
					}
					else{
						first = rabbit::std::rex_charnode(exp,SQTrue);
					}
				}
			}
			if(first!=-1){
				int64_t c = first;
				exp->_nodes[chain].next = c;
			}
			/* hack? */
			exp->_nodes[ret].left = exp->_nodes[ret].next;
			exp->_nodes[ret].next = -1;
			return ret;
		}
		
		static int64_t rex_parsenumber(SQRex *exp)
		{
			int64_t ret = *exp->_p-'0';
			int64_t positions = 10;
			exp->_p++;
			while(isdigit(*exp->_p)) {
				ret = ret*10+(*exp->_p++-'0');
				if(positions==1000000000) rabbit::std::rex_error(exp,"overflow in numeric constant");
				positions *= 10;
			};
			return ret;
		}
		
		static int64_t rex_element(SQRex *exp)
		{
			int64_t ret = -1;
			switch(*exp->_p)
			{
			case '(': {
				int64_t expr;
				exp->_p++;
		
		
				if(*exp->_p =='?') {
					exp->_p++;
					rabbit::std::rex_expect(exp,':');
					expr = rabbit::std::rex_newnode(exp,OP_NOCAPEXPR);
				}
				else
					expr = rabbit::std::rex_newnode(exp,OP_EXPR);
				int64_t newn = rabbit::std::rex_list(exp);
				exp->_nodes[expr].left = newn;
				ret = expr;
				rabbit::std::rex_expect(exp,')');
					  }
					  break;
			case '[':
				exp->_p++;
				ret = rabbit::std::rex_class(exp);
				rabbit::std::rex_expect(exp,']');
				break;
			case SQREX_SYMBOL_END_OF_STRING: exp->_p++; ret = rabbit::std::rex_newnode(exp,OP_EOL);break;
			case SQREX_SYMBOL_ANY_CHAR: exp->_p++; ret = rabbit::std::rex_newnode(exp,OP_DOT);break;
			default:
				ret = rabbit::std::rex_charnode(exp,SQFalse);
				break;
			}
		
		
			rabbit::Bool isgreedy = SQFalse;
			unsigned short p0 = 0, p1 = 0;
			switch(*exp->_p){
				case SQREX_SYMBOL_GREEDY_ZERO_OR_MORE: p0 = 0; p1 = 0xFFFF; exp->_p++; isgreedy = SQTrue; break;
				case SQREX_SYMBOL_GREEDY_ONE_OR_MORE: p0 = 1; p1 = 0xFFFF; exp->_p++; isgreedy = SQTrue; break;
				case SQREX_SYMBOL_GREEDY_ZERO_OR_ONE: p0 = 0; p1 = 1; exp->_p++; isgreedy = SQTrue; break;
				case '{':
					exp->_p++;
					if(!isdigit(*exp->_p)) rabbit::std::rex_error(exp,"number expected");
					p0 = (unsigned short)rabbit::std::rex_parsenumber(exp);
					/*******************************/
					switch(*exp->_p) {
				case '}':
					p1 = p0; exp->_p++;
					break;
				case ',':
					exp->_p++;
					p1 = 0xFFFF;
					if(isdigit(*exp->_p)){
						p1 = (unsigned short)rabbit::std::rex_parsenumber(exp);
					}
					rabbit::std::rex_expect(exp,'}');
					break;
				default:
					rabbit::std::rex_error(exp,", or } expected");
					}
					/*******************************/
					isgreedy = SQTrue;
					break;
		
			}
			if(isgreedy) {
				int64_t nnode = rabbit::std::rex_newnode(exp,OP_GREEDY);
				exp->_nodes[nnode].left = ret;
				exp->_nodes[nnode].right = ((p0)<<16)|p1;
				ret = nnode;
			}
		
			if((*exp->_p != SQREX_SYMBOL_BRANCH) && (*exp->_p != ')') && (*exp->_p != SQREX_SYMBOL_GREEDY_ZERO_OR_MORE) && (*exp->_p != SQREX_SYMBOL_GREEDY_ONE_OR_MORE) && (*exp->_p != '\0')) {
				int64_t nnode = rabbit::std::rex_element(exp);
				exp->_nodes[ret].next = nnode;
			}
		
			return ret;
		}
		
		static int64_t rex_list(SQRex *exp)
		{
			int64_t ret=-1,e;
			if(*exp->_p == SQREX_SYMBOL_BEGINNING_OF_STRING) {
				exp->_p++;
				ret = rabbit::std::rex_newnode(exp,OP_BOL);
			}
			e = rabbit::std::rex_element(exp);
			if(ret != -1) {
				exp->_nodes[ret].next = e;
			}
			else ret = e;
		
			if(*exp->_p == SQREX_SYMBOL_BRANCH) {
				int64_t temp,tright;
				exp->_p++;
				temp = rabbit::std::rex_newnode(exp,OP_OR);
				exp->_nodes[temp].left = ret;
				tright = rabbit::std::rex_list(exp);
				exp->_nodes[temp].right = tright;
				ret = temp;
			}
			return ret;
		}
		
		static rabbit::Bool rex_matchcclass(int64_t cclass,char c)
		{
			switch(cclass) {
			case 'a': return isalpha(c)?SQTrue:SQFalse;
			case 'A': return !isalpha(c)?SQTrue:SQFalse;
			case 'w': return (isalnum(c) || c == '_')?SQTrue:SQFalse;
			case 'W': return (!isalnum(c) && c != '_')?SQTrue:SQFalse;
			case 's': return isspace(c)?SQTrue:SQFalse;
			case 'S': return !isspace(c)?SQTrue:SQFalse;
			case 'd': return isdigit(c)?SQTrue:SQFalse;
			case 'D': return !isdigit(c)?SQTrue:SQFalse;
			case 'x': return isxdigit(c)?SQTrue:SQFalse;
			case 'X': return !isxdigit(c)?SQTrue:SQFalse;
			case 'c': return iscntrl(c)?SQTrue:SQFalse;
			case 'C': return !iscntrl(c)?SQTrue:SQFalse;
			case 'p': return ispunct(c)?SQTrue:SQFalse;
			case 'P': return !ispunct(c)?SQTrue:SQFalse;
			case 'l': return islower(c)?SQTrue:SQFalse;
			case 'u': return isupper(c)?SQTrue:SQFalse;
			}
			return SQFalse; /*cannot happen*/
		}
		
		static rabbit::Bool rex_matchclass(SQRex* exp,SQRexNode *node,char c)
		{
			do {
				switch(node->type) {
					case OP_RANGE:
						if(c >= node->left && c <= node->right) return SQTrue;
						break;
					case OP_CCLASS:
						if(rabbit::std::rex_matchcclass(node->left,c)) return SQTrue;
						break;
					default:
						if(c == node->type)return SQTrue;
				}
			} while((node->next != -1) && (node = &exp->_nodes[node->next]));
			return SQFalse;
		}
		
		static const char * rex_matchnode(SQRex* exp,SQRexNode *node,const char *str,SQRexNode *next)
		{
		
			SQRexNodeType type = node->type;
			switch(type) {
			case OP_GREEDY: {
				//SQRexNode *greedystop = (node->next != -1) ? &exp->_nodes[node->next] : NULL;
				SQRexNode *greedystop = NULL;
				int64_t p0 = (node->right >> 16)&0x0000FFFF, p1 = node->right&0x0000FFFF, nmaches = 0;
				const char *s=str, *good = str;
		
				if(node->next != -1) {
					greedystop = &exp->_nodes[node->next];
				}
				else {
					greedystop = next;
				}
		
				while((nmaches == 0xFFFF || nmaches < p1)) {
		
					const char *stop;
					if(!(s = rabbit::std::rex_matchnode(exp,&exp->_nodes[node->left],s,greedystop)))
						break;
					nmaches++;
					good=s;
					if(greedystop) {
						//checks that 0 matches satisfy the expression(if so skips)
						//if not would always stop(for instance if is a '?')
						if(greedystop->type != OP_GREEDY ||
						(greedystop->type == OP_GREEDY && ((greedystop->right >> 16)&0x0000FFFF) != 0))
						{
							SQRexNode *gnext = NULL;
							if(greedystop->next != -1) {
								gnext = &exp->_nodes[greedystop->next];
							}else if(next && next->next != -1){
								gnext = &exp->_nodes[next->next];
							}
							stop = rabbit::std::rex_matchnode(exp,greedystop,s,gnext);
							if(stop) {
								//if satisfied stop it
								if(p0 == p1 && p0 == nmaches) break;
								else if(nmaches >= p0 && p1 == 0xFFFF) break;
								else if(nmaches >= p0 && nmaches <= p1) break;
							}
						}
					}
		
					if(s >= exp->_eol)
						break;
				}
				if(p0 == p1 && p0 == nmaches) return good;
				else if(nmaches >= p0 && p1 == 0xFFFF) return good;
				else if(nmaches >= p0 && nmaches <= p1) return good;
				return NULL;
			}
			case OP_OR: {
					const char *asd = str;
					SQRexNode *temp=&exp->_nodes[node->left];
					while( (asd = rabbit::std::rex_matchnode(exp,temp,asd,NULL)) ) {
						if(temp->next != -1)
							temp = &exp->_nodes[temp->next];
						else
							return asd;
					}
					asd = str;
					temp = &exp->_nodes[node->right];
					while( (asd = rabbit::std::rex_matchnode(exp,temp,asd,NULL)) ) {
						if(temp->next != -1)
							temp = &exp->_nodes[temp->next];
						else
							return asd;
					}
					return NULL;
					break;
			}
			case OP_EXPR:
			case OP_NOCAPEXPR:{
					SQRexNode *n = &exp->_nodes[node->left];
					const char *cur = str;
					int64_t capture = -1;
					if(node->type != OP_NOCAPEXPR && node->right == exp->_currsubexp) {
						capture = exp->_currsubexp;
						exp->_matches[capture].begin = cur;
						exp->_currsubexp++;
					}
					int64_t tempcap = exp->_currsubexp;
					do {
						SQRexNode *subnext = NULL;
						if(n->next != -1) {
							subnext = &exp->_nodes[n->next];
						}else {
							subnext = next;
						}
						if(!(cur = rabbit::std::rex_matchnode(exp,n,cur,subnext))) {
							if(capture != -1){
								exp->_matches[capture].begin = 0;
								exp->_matches[capture].len = 0;
							}
							return NULL;
						}
					} while((n->next != -1) && (n = &exp->_nodes[n->next]));
		
					exp->_currsubexp = tempcap;
					if(capture != -1)
						exp->_matches[capture].len = cur - exp->_matches[capture].begin;
					return cur;
			}
			case OP_WB:
				if((str == exp->_bol && !isspace(*str))
				 || (str == exp->_eol && !isspace(*(str-1)))
				 || (!isspace(*str) && isspace(*(str+1)))
				 || (isspace(*str) && !isspace(*(str+1))) ) {
					return (node->left == 'b')?str:NULL;
				}
				return (node->left == 'b')?NULL:str;
			case OP_BOL:
				if(str == exp->_bol) return str;
				return NULL;
			case OP_EOL:
				if(str == exp->_eol) return str;
				return NULL;
			case OP_DOT:{
				if (str == exp->_eol) return NULL;
				str++;
						}
				return str;
			case OP_NCLASS:
			case OP_CLASS:
				if (str == exp->_eol) return NULL;
				if(rabbit::std::rex_matchclass(exp,&exp->_nodes[node->left],*str)?(type == OP_CLASS?SQTrue:SQFalse):(type == OP_NCLASS?SQTrue:SQFalse)) {
					str++;
					return str;
				}
				return NULL;
			case OP_CCLASS:
				if (str == exp->_eol) return NULL;
				if(rabbit::std::rex_matchcclass(node->left,*str)) {
					str++;
					return str;
				}
				return NULL;
			case OP_MB:
				{
					int64_t cb = node->left; //char that opens a balanced expression
					if(*str != cb) return NULL; // string doesnt start with open char
					int64_t ce = node->right; //char that closes a balanced expression
					int64_t cont = 1;
					const char *streol = exp->_eol;
					while (++str < streol) {
					  if (*str == ce) {
						if (--cont == 0) {
							return ++str;
						}
					  }
					  else if (*str == cb) cont++;
					}
				}
				return NULL; // string ends out of balance
			default: /* char */
				if (str == exp->_eol) return NULL;
				if(*str != node->type) return NULL;
				str++;
				return str;
			}
			return NULL;
		}
	}
}
/* public api */
rabbit::std::SQRex *rabbit::std::rex_compile(const char *pattern,const char **error)
{
	SQRex * volatile exp = (SQRex *)sq_malloc(sizeof(SQRex)); // "volatile" is needed for setjmp()
	exp->_eol = exp->_bol = NULL;
	exp->_p = pattern;
	exp->_nallocated = (int64_t)strlen(pattern) * sizeof(char);
	exp->_nodes = (SQRexNode *)sq_malloc(exp->_nallocated * sizeof(SQRexNode));
	exp->_nsize = 0;
	exp->_matches = 0;
	exp->_nsubexpr = 0;
	exp->_first = rabbit::std::rex_newnode(exp,OP_EXPR);
	exp->_error = error;
	exp->_jmpbuf = sq_malloc(sizeof(jmp_buf));
	if(setjmp(*((jmp_buf*)exp->_jmpbuf)) == 0) {
		int64_t res = rabbit::std::rex_list(exp);
		exp->_nodes[exp->_first].left = res;
		if(*exp->_p!='\0')
			rabbit::std::rex_error(exp,"unexpected character");
#ifdef _DEBUG
		{
			int64_t nsize,i;
			SQRexNode *t;
			nsize = exp->_nsize;
			t = &exp->_nodes[0];
			printf("\n");
			for(i = 0;i < nsize; i++) {
				if(exp->_nodes[i].type>UINT8_MAX)
					printf("[%02d] %10s ", (int32_t)i,g_nnames[exp->_nodes[i].type-UINT8_MAX]);
				else
					printf("[%02d] %10c ", (int32_t)i,exp->_nodes[i].type);
				printf("left %02d right %02d next %02d\n", (int32_t)exp->_nodes[i].left, (int32_t)exp->_nodes[i].right, (int32_t)exp->_nodes[i].next);
			}
			printf("\n");
		}
#endif
		exp->_matches = (SQRexMatch *) sq_malloc(exp->_nsubexpr * sizeof(SQRexMatch));
		memset(exp->_matches,0,exp->_nsubexpr * sizeof(SQRexMatch));
	}
	else{
		rabbit::std::rex_free(exp);
		return NULL;
	}
	return exp;
}

void rabbit::std::rex_free(SQRex *exp)
{
	if(exp) {
		if(exp->_nodes) sq_free(exp->_nodes,exp->_nallocated * sizeof(SQRexNode));
		if(exp->_jmpbuf) sq_free(exp->_jmpbuf,sizeof(jmp_buf));
		if(exp->_matches) sq_free(exp->_matches,exp->_nsubexpr * sizeof(SQRexMatch));
		sq_free(exp,sizeof(SQRex));
	}
}

rabbit::Bool rabbit::std::rex_match(SQRex* exp,const char* text)
{
	const char* res = NULL;
	exp->_bol = text;
	exp->_eol = text + strlen(text);
	exp->_currsubexp = 0;
	res = rabbit::std::rex_matchnode(exp,exp->_nodes,text,NULL);
	if(res == NULL || res != exp->_eol)
		return SQFalse;
	return SQTrue;
}

rabbit::Bool rabbit::std::rex_searchrange(SQRex* exp,const char* text_begin,const char* text_end,const char** out_begin, const char** out_end)
{
	const char *cur = NULL;
	int64_t node = exp->_first;
	if(text_begin >= text_end) return SQFalse;
	exp->_bol = text_begin;
	exp->_eol = text_end;
	do {
		cur = text_begin;
		while(node != -1) {
			exp->_currsubexp = 0;
			cur = rabbit::std::rex_matchnode(exp,&exp->_nodes[node],cur,NULL);
			if(!cur)
				break;
			node = exp->_nodes[node].next;
		}
		text_begin++;
	} while(cur == NULL && text_begin != text_end);

	if(cur == NULL)
		return SQFalse;

	--text_begin;

	if(out_begin) *out_begin = text_begin;
	if(out_end) *out_end = cur;
	return SQTrue;
}

rabbit::Bool rabbit::std::rex_search(SQRex* exp,const char* text, const char** out_begin, const char** out_end)
{
	return rabbit::std::rex_searchrange(exp,text,text + strlen(text),out_begin,out_end);
}

int64_t rabbit::std::rex_getsubexpcount(SQRex* exp)
{
	return exp->_nsubexpr;
}

rabbit::Bool rabbit::std::rex_getsubexp(SQRex* exp, int64_t n, SQRexMatch *subexp)
{
	if( n<0 || n >= exp->_nsubexpr) return SQFalse;
	*subexp = exp->_matches[n];
	return SQTrue;
}

