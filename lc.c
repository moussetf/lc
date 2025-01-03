#include <ctype.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <readline/readline.h>
#include <readline/history.h>

struct term;
typedef struct term term;

bool strat_innermost = false;
bool strat_weak = true;
bool single_step = false;
bool silent = false;

/****** Symbols **********/

typedef struct sym { char c; int l; term *b; struct sym *gu, *gd, *nx; } sym;

void psym(sym *s) { if (s) { psym(s->gu); fputc(s->c, stdout); } }

sym *symbol(char *name) {
	static sym *symtab = NULL;
	sym *node = NULL, **place = &symtab;
	char c, *start = name;
	while ((isalnum(c = *(name++)))) {
		while (*place && (*place)->c != c) place = &(*place)->nx;
		if (!*place) {
			*place = calloc(1, sizeof(sym));
			**place = (sym){.c=c, .l=name-start, .nx=NULL, .gu=node};
		}
		place = &((node = *place)->gd);
	}
	return node;
}

/****** Terms ************/

struct term { enum typ { VAR, ABS, APP } g; sym *x; struct term *t; struct term *s; };

term *mkvar(sym *x)           { term *r = calloc(1, sizeof(term)); *r = (term){.g=VAR, .x=x};       return r; }
term *mkabs(sym *x, term *t)  { term *r = calloc(1, sizeof(term)); *r = (term){.g=ABS, .x=x, .t=t}; return r; }
term *mkapp(term *t, term *s) { term *r = calloc(1, sizeof(term)); *r = (term){.g=APP, .t=t, .s=s}; return r; }

#define var(X, T)    ((T)->g == VAR && ((X) = (T)->x))
#define abs(X, U, T) ((T)->g == ABS && ((X) = (T)->x) && ((U) = (T)->t))
#define app(U, V, T) ((T)->g == APP && ((U) = (T)->t) && ((V) = (T)->s))

void tfree(term *t) {
	sym *_; term *e, *f;
	if      (var(_, t))    { free(t); }
	else if (abs(_, e, t)) { tfree(e); free(t); }
	else if (app(e, f, t)) { tfree(e); tfree(f); free(t); }
}

term *clone(term *t) {
	sym *x; term *e, *f;
	if      (var(x, t))    { return mkvar(x); }
	else if (abs(x, e, t)) { return mkabs(x, clone(e)); }
	else if (app(e, f, t)) { return mkapp(clone(e), clone(f)); }
}

void sub(term *t, sym *var, term *new) {
	sym *x; term *e, *f;
	if      (var(x, t) && x == var)    { *t = *(e = clone(new)); free(e); }
	else if (abs(x, e, t) && x != var) { sub(e, var, new); }
	else if (app(e, f, t))             { sub(e, var, new); sub(f, var, new); }
}

void pterm(term *t) {
	sym *x; term *e, *f, *_;
	if      (var(x, t))    { psym(x); }
	else if (abs(x, e, t)) { printf("(%lc", L'λ'); psym(x); printf("."); pterm(e); printf(")"); }
	else if (app(e, f, t)) { pterm(e); printf(" "); if app(_, _, f) { printf("("); pterm(f); printf(")"); } else { pterm(f); } }
}

bool uses(term *t, sym *var) {
	sym *x; term *e, *f;
	if      (var(x, t))    { return x == var; }
	else if (abs(x, e, t)) { return var == x || uses(e, var); }
	else if (app(e, f, t)) { return uses(e, var) || uses(f, var); }
}

bool uses_free(term *t, sym *var) {
	sym *x; term *e, *f;
	if      (var(x, t))    { return x == var; }
	else if (abs(x, e, t)) { return var != x && uses_free(e, var); }
	else if (app(e, f, t)) { return uses_free(e, var) || uses_free(f, var); }
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
	sym *x; term *e, *f, *g;
	if (abs(x, e, t) && x != var && uses_free(e, var)) {
		if (uses_free(s, x)) {
			t->x = gensym(e, s);
			sub(e, x, (g = mkvar(t->x))); free(g);
			alpha(e, var, s);
			return true;
		}
		return alpha(e, var, s);
	}
	else if (app(e, f, t)) {
		return alpha(e, var, s) || alpha(f, var, s);
	}
	return false;
}

rtype reduce(term *t) {
	sym *x, *y; term *e, *f, *g; rtype rt;
	if (abs(x, e, t)) {
		if (strat_weak) return NONE;
		if (app(f, g, e) && var(y, g) && y == x && !uses_free(f, x)) {
			*t = *f;
			free(e); free(f); free(g);
			return ETA;
		}
		f = x->b; x->b = NULL; rt = reduce(e); x->b = f;
		return rt;
	} else if (app(e, f, t)) {
		if (var(x, e) && x->b) {
			*e = *(g = clone(x->b)); free(g);
			return EXP;
		}
		else if (strat_innermost && ((rt = reduce(e)) != NONE || (rt = reduce(f)) != NONE)) {
			return rt;
		}
		else if (abs(x, g, e)) {
			if (alpha(g, x, f)) return ALPHA;
			else {
				sub(g, x, f);
				*t = *g;
				free(e); tfree(f); free(g);
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
parser(p_symbol) { ws; sym *s = symbol(*buf); if (s) *buf += s->l; return s; }
parser(p_var) { let(x, p_symbol); return mkvar(x); }
parser(p_apos) { d('\''); success; }
parser(p_utf8lam) { ws; c('\xce'); c('\xbb'); success; }
parser(p_lambda) { try(p_utf8lam); try(p_apos); fail; }
parser(p_abs) { let(_, p_lambda); let(x, p_symbol); d('.'); let(t, p_expr); return mkabs(x, t); }
parser(p_parexpr) { d('('); let(t, p_expr); d(')'); return t; }
parser(p_term) { try(p_var); try(p_parexpr); try(p_abs); fail; }
parser(p_expr) { let(t, p_term); while (true) { with(s, p_term) { t = mkapp(t, s); } else break; } return t; }

parser(p_assmt) { let(s, p_symbol); d('='); let(t, p_expr); d('\0'); ((sym *)s)->b = t; return mkvar(s); }
parser(p_expr_or_assmt) { try(p_assmt); let(t, p_expr); d('\0'); return t; }

// Commands
void *const ON = (void *)1;
void *const OFF = (void *)2;
parser(p_on) { s("on"); return ON; }
parser(p_toggle) { ws; try(p_on); s("off"); return OFF; }
parser(p_innermost) { s("!inner "); let(t, p_toggle); strat_innermost = (t == ON); success; }
parser(p_strong) { s("!strong "); let(t, p_toggle); strat_weak = (t == OFF); success; }
parser(p_step) { s("!step "); let(t, p_toggle); single_step = (t == ON); success; }
parser(p_unset) { s("!unset "); let(t, p_symbol); ((sym *)t)->b = NULL; success; }
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

// todo report error
void process_line(char *buf) {
	term *t = NULL, *s = NULL;
	sym *x = NULL; rtype rt = NONE;
	if (parse(p_cmd, buf)) goto cleanup;
	t = s = parse(p_expr_or_assmt, buf);
	if (!s) { printf("no parse\n"); goto cleanup; }
	if (var(x, s) && x->b) s = x->b;
	do {
		if (single_step) {
			printf("(%lc) ", rt ? rt : L'*');
			pterm(s); fflush(stdout);
			if (!step_pause()) { interrupt = true; break; }
		}
	} while (!interrupt && (rt = reduce(s)));
	if (interrupt) printf("Interrupted\n");
	else if (!single_step && !silent) { pterm(s); fputc('\n', stdout); fflush(stdout); }
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

	if (argc == 2) {
		size_t bufsize = 0x10000;
		buf = calloc(bufsize, 1);
		FILE *f = fopen(argv[1], "r");
		if (!f) { perror(argv[1]); return 1; }
		silent = true;
		while (fgets(buf, bufsize, f)) {
			if (0 != strlen(buf) && buf[0] != '\n') process_line(buf);
		}
		silent = false;
		if (ferror(f)) { perror("fgets"); return 1; }
		if (fclose(f) == EOF) { perror("fclose"); return 1; }
		free(buf);
	}
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
