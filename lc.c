#include <ctype.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <readline/history.h>
#include <readline/readline.h>

struct term;
typedef struct term term;

bool strat_innermost = false;
bool strat_weak = true;
bool single_step = false;
bool echo = false;

/****** Symbols **********/

typedef struct sym {
	int len; term *bdg;       // symbol properties (length and binding)
	char c;                   // last character of symbol
	struct sym *gu, *gd, *nx; // trie pointers (up/down/next)
} sym;

void print_sym(sym *s) {
	if (s) {
		char *buffer = calloc(1+s->len, sizeof(char));
		int i = s->len;
		do buffer[--i] = s->c; while ((s = s->gu));
		fputs(buffer, stdout);
		free(buffer);
	}
}

sym *symbol(char *name) {
	static sym *symtab = NULL;
	sym *node = NULL, **place = &symtab;
	char c, *start = name;
	while ((isalnum(c = *(name++)))) {
		while (*place && (*place)->c != c) place = &(*place)->nx;
		if (!*place) {
			*place = calloc(1, sizeof(sym));
			**place = (sym){.len=name-start, .bdg=NULL, .c=c, .nx=NULL, .gu=node, .gd=NULL};
		}
		place = &((node = *place)->gd);
	}
	return node;
}

/****** Terms ************/

struct term {
	enum typ { VAR, ABS, APP } g; // type of term (variable, abstraction, application)
	union {
		struct { sym *name; }; // if VAR
		struct { sym *var; struct term *body; }; // if ABS
		struct { struct term *t; struct term *s; }; // if APP
	};
};

term *mkvar(sym *name) {
	term *r = malloc(sizeof(term));
	*r = (term){.g=VAR, .name=name};
	return r;
}
term *mkabs(sym *var, term *body) {
	term *r = malloc(sizeof(term));
	*r = (term){.g=ABS, .var=var, .body=body};
	return r;
}
term *mkapp(term *t, term *s) {
	term *r = malloc(sizeof(term));
	*r = (term){.g=APP, .t=t, .s=s};
	return r;
}

void tfree(term *t) {
	switch (t->g) {
		case VAR: free(t); break;
		case ABS: tfree(t->body); free(t); break;
		case APP: tfree(t->s); tfree(t->t); free(t); break;
	}
}

term *clone(term *t) {
	switch (t->g) {
		case VAR: return mkvar(t->name);
		case ABS: return mkabs(t->var, clone(t->body));
		case APP: return mkapp(clone(t->t), clone(t->s));
	}
}

void sub(term *t, sym *var, term *new) {
	term *s;
	switch (t->g) {
		case VAR: if (var == t->name) { *t = *(s = clone(new)); free(s); } break;
		case ABS: if (var != t->var) sub(t->body, var, new); break;
		case APP: sub(t->t, var, new); sub(t->s, var, new); break;
	}
}

void pterm(term *t) {
	switch (t->g) {
		case VAR: print_sym(t->name); break;
		case ABS:
			printf("(%lc", L'λ');
			print_sym(t->var); printf("."); pterm(t->body);
			printf(")");
			break;
		case APP:
			pterm(t->t); printf(" ");
			if (t->s->g == APP) {
				printf("("); pterm(t->s); printf(")");
			} else {
				pterm(t->s);
			}
			break;
	}
}

bool uses(term *t, sym *var) {
	switch (t->g) {
		case VAR: return var == t->name;
		case ABS: return var == t->var || uses(t->body, var);
		case APP: return uses(t->t, var) || uses(t->s, var);
	}
}

bool uses_free(term *t, sym *var) {
	switch (t->g) {
		case VAR: return var == t->name;
		case ABS: return var != t->var && uses_free(t->body, var);
		case APP: return uses_free(t->t, var) || uses_free(t->s, var);
	}
}

// Generate a symbol that does not appear in t (generally) or in s (as a free variable)
sym *gensym(term *t, term *s) {
	char name[16]; int gen = 0; sym *x = NULL;
	do {
		memset(name, 0, sizeof(name));
		int i = 0, k = gen++;
		do { name[i++] = (k%26) + 'a'; } while((k = k/26));
		x = symbol(name);
	} while (uses(t, x) || uses_free(s, x));
	return x;
}

/****** Reductions *******/

typedef enum rtype { NONE = 0, ALPHA = L'α', BETA = L'β', ETA = L'η' , EXP = L'=' } rtype;

// Resolve name clashes that would occur when free occurrences of var are
// replaced by s in t. Specifically, abstraction variables must be renamed if
// 1. they contain var as a free variable in their body, and
// 2. the abstraction variable appears as a free variable in s
bool alpha(term *t, sym *var, term *s) {
	term *tmp; sym *x;
	switch (t->g) {
		case VAR: return false;
		case ABS:
			if ((x = t->var) != var && uses_free(t->body, var)) {
				if (uses_free(s, t->var)) {
					t->var = gensym(t->body, s);
					sub(t->body, x, (tmp = mkvar(t->var)));
					free(tmp);
					alpha(t->body, var, s);
					return true;
				}
				return alpha(t->body, var, s);
			}
			return false;
		case APP:
			return alpha(t->t, var, s) || alpha(t->s, var, s);
	}
}

rtype reduce(term *t) {
	term *e, *f, *g; rtype rt;
	switch (t->g) {
		case VAR: break;
		case ABS:
			if (strat_weak) return NONE;
			e = t->body; f = e->t; g = e->s;
			if (e->g == APP && g->g == VAR && g->name == t->var && !uses_free(f, t->var)) {
				*t = *f;
				free(e); free(f); free(g);
				return ETA;
			}
			f = t->var->bdg; t->var->bdg = NULL; rt = reduce(t->body); t->var->bdg = f;
			return rt;
		case APP:
			e = t->t; f = t->s;
			if (e->g == VAR && e->name->bdg) {
				*e = *(g = clone(e->name->bdg)); free(g);
				return EXP;
			}
			else if (strat_innermost && ((rt = reduce(e)) != NONE || (rt = reduce(f)) != NONE)) {
				return rt;
			}
			else if (e->g == ABS) {
				if (alpha(e->body, e->var, f)) {
					return ALPHA;
				} else {
					sub(e->body, e->var, f);
					*t = *e->body;
					free(e->body); free(e); tfree(f);
					return BETA;
				}
			}
			return (rt = reduce(e)) ? rt : reduce(f);
	}
	return NONE;
}

/****** Line parser ******/

#define parser(X) void *X(char **buf)
#define fail return NULL;
#define success return (void *)1;
#define with(R, P) void *R;  do { char *_s = *buf; R = P(buf); if (!R) { *buf = _s; } } while(false); if (R)
#define let(R, P) with(R, P) { } else fail;
#define try(P) do { with(_t, P) return _t; } while (false);
#define ws while (**buf == ' ' || **buf == '\n' || **buf == '\r') { (*buf)++; }
#define c(C) if (**buf == C) { (*buf)++; } else fail;
#define d(C) ws; c(C);
#define s(S) do { char *s = *buf; char *t = S; while (*s && *t && *s == *t) {s++;t++;}; \
	if (s - *buf == strlen(S)) *buf = s; else fail; } while (false);

void *parse(void *(*p)(char **), char *buf) { char *b = buf; return p(&b); }

// Expressions and assignments
parser(p_expr);
parser(p_symbol) { ws; sym *s = symbol(*buf); if (s) *buf += s->len; return s; }
parser(p_var) { let(x, p_symbol); return mkvar(x); }
parser(p_apos) { d('\''); success; }
parser(p_utf8lam) { ws; c('\xce'); c('\xbb'); success; }
parser(p_lambda) { try(p_utf8lam); try(p_apos); fail; }
parser(p_abs) { let(_, p_lambda); let(x, p_symbol); d('.'); let(t, p_expr); return mkabs(x, t); }
parser(p_parexpr) { d('('); let(t, p_expr); d(')'); return t; }
parser(p_term) { try(p_var); try(p_parexpr); try(p_abs); fail; }
parser(p_expr) { let(t, p_term); while (true) { with(s, p_term) { t = mkapp(t, s); } else break; } return t; }

parser(p_assmt) { let(s, p_symbol); d('='); let(t, p_expr); d('\0'); ((sym *)s)->bdg = t; return mkvar(s); }
parser(p_expr_or_assmt) { try(p_assmt); let(t, p_expr); d('\0'); return t; }

// Commands
void *const ON = (void *)1;
void *const OFF = (void *)2;
parser(p_on) { s("on"); return ON; }
parser(p_toggle) { ws; try(p_on); s("off"); return OFF; }
parser(p_innermost) { s("!inner "); let(t, p_toggle); strat_innermost = (t == ON); success; }
parser(p_strong) { s("!strong "); let(t, p_toggle); strat_weak = (t == OFF); success; }
parser(p_step) { s("!step "); let(t, p_toggle); single_step = (t == ON); success; }
parser(p_unset) { s("!unset "); let(t, p_symbol); ((sym *)t)->bdg = NULL; success; }
parser(p_cmd) { ws; try(p_innermost); try(p_strong); try(p_step); try(p_unset); fail; }

/****** Main loop ********/

volatile sig_atomic_t interrupt = 0;
void sigint(int s) { interrupt = 1; rl_done = 1;}

bool step_pause() {
	char c;
	while ((c = fgetc(stdin)) != '\n') {
		if (c == EOF) {
			clearerr(stdin);
			fputc('\n', stdout);
			return false;
		}
	}
	return true;
}

void process_line(char *buf) {
	term *t = NULL, *s = NULL;
	rtype rt = NONE;
	if (parse(p_cmd, buf)) goto cleanup;
	t = s = parse(p_expr_or_assmt, buf);
	if (!s) { printf("no parse\n"); goto cleanup; }
	if (s->g == VAR && s->name->bdg) s = s->name->bdg;
	do {
		if (single_step) {
			printf("(%lc) ", rt ? rt : L'*');
			pterm(s); fflush(stdout);
			if (!step_pause()) { interrupt = true; break; }
		}
	} while (!interrupt && (rt = reduce(s)));
	if (interrupt) printf("Interrupted\n");
	else if (!single_step && echo) { pterm(s); fputc('\n', stdout); fflush(stdout); }
cleanup:
	if (t) tfree(t);
	interrupt = 0;
}

int main(int argc, char *argv[]) {
	struct sigaction handler;
	char *buf;

	handler.sa_handler = sigint;
	sigemptyset(&handler.sa_mask);
	handler.sa_flags = 0;
	sigaction(SIGINT, &handler, NULL);
	setlocale(LC_ALL, "");

	int i;
	for (i = 1; i < argc; i++) {
		size_t bufsize = 0x10000;
		buf = calloc(bufsize, 1);
		FILE *f = fopen(argv[i], "r");
		if (!f) { perror(argv[i]); return 1; }
		while (fgets(buf, bufsize, f)) if (0 != strlen(buf) && buf[0] != '\n') process_line(buf);
		if (ferror(f)) { perror("fgets"); return 1; }
		if (fclose(f) == EOF) { perror("fclose"); return 1; }
		free(buf);
	}
	echo = true;
	using_history();
	rl_catch_signals = 0;
	rl_bind_key ('\t', rl_insert);
	while ((buf = readline("+ "))) {
		if (0 != strlen(buf))  {
			add_history(buf);
			process_line(buf);
		}
		free(buf);
	}
	return 0;
}
