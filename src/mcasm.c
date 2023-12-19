#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOKEN_LENGTH 255
#define CMDS_PER_GRP 16
#define MAX_OPTIONS 4 // max identifiers to define an cmd option
#define CMDSET_BITS 3
#define PAGE_BITS 1
#define CMD_BITS 8
#define STEP_BITS 4
// construct the microcode address for cmdId at the current cmdSet and page
#define MC_ADDR(cmdId) \
	(((((cmdSet << PAGE_BITS) | page) << CMD_BITS) | (cmdId)) << STEP_BITS)

#define NELEMS(a) ((int)(sizeof(a) / sizeof((a)[0]))) // copied from LCC
#define SET_BITS(n) ((1 << (n)) - 1) // value with lower n bits set

#define SYSTEM_TOKENS             \
	XX(T_EXPORT, "export")        \
	XX(T_DEF, "def")              \
	XX(T_SET, "set")              \
	XX(T_ZET, "zet")              \
	XX(T_PORT, "port")            \
	XX(T_FORGET, "forget")        \
	XX(T_CMDSET, "cmdSet")        \
	XX(T_PAGE, "page")            \
	XX(T_GRP, "grp")              \
	XX(T_COMMENT_START, ";")      \
	XX(T_ON, "on")                \
	XX(T_OFF, "off")              \
	XX(T_EQ, "=")                 \
	/* Cmd with tst bit set */    \
	XX(T_EQ_TEST, "=?")           \
	/* Immediate cmd */           \
	XX(T_EQ_IMM, "=#")            \
	/* Immediate cmd; value */    \
	/* from cmdGrp local label */ \
	XX(T_EQ_LABEL, "=:")          \
	XX(T_CMD_GROUP_START, "{")    \
	XX(T_CMD_GROUP_END, "}")      \
	XX(T_LABEL_SEP, ":")          \
	XX(T_NL, "\n")                \
	XX(T_EOF, "") /* must be the last */

#define CMD_TYPE_IMM 0
#define CMD_TYPE_PORT 1
#define CMD_TST 0
#define CMD_NO_TST 1

enum errClass { CONTINUE, TRACE, WARN, ERROR, FATAL };
#define RED
const char *errClassStr[] = {"", "", "\033[95mwarning: \033[0m",
							 "\033[91merror: \033[0m",
							 "\033[91mfatal: \033[0m"};

struct cmd {
	const char *referencedLabel;
	const char *label;
	const char *dOpt[MAX_OPTIONS];
	const char *dstName;
	const char *assignType;
	const char *sOpt[MAX_OPTIONS];
	const char *srcName;
	bool isUsed; // true if this slot, or any later, are defined
	uint16_t cv; // the microcode Command Value
};

/* linked list of interned strings */
struct strings {
	const char *str;
	int len;
	struct strings *next;
} *interned = NULL;

struct symbols;
typedef struct symbols *Symbol;

struct symbols {
	const char *id;
	int value;
	Symbol next;
} *symbols = NULL, *ports = NULL;

#define XX(name, str) const char *name;
SYSTEM_TOKENS
#undef XX

FILE *dst;
const char *fileName;
int line = 1;
int col = 1;
bool trace = false;
bool parsing = false;
int exitStatus = EXIT_SUCCESS;

int cmdSet;	 // current command set
int page;	 // current command page
bool export; // true if exporting newly defined identifiers

/* Command layout
	  iDDDDdddtSSSSsss
   where:
	i    0 => immediate instruction;
		source value is a constant with value tSSSSsss
	DDDD destination option
	ddd  destination port
	t    0 => test and conditionally execute
	SSSS source option
	sss  source port
*/

// extract parts of a Command Value
int cvType(uint16_t cv) {
	return (cv >> 15) & 1;
}
bool cvIsImm(uint16_t cv) {
	return cvType(cv) == CMD_TYPE_IMM;
}
int cvDOpt(uint16_t cv) {
	return (cv >> 11) & 0xf;
}
int cvDst(uint16_t cv) {
	return (cv >> 8) & SET_BITS(3);
}
int cvDSpec(uint16_t cv) {
	return (cv >> 8) & SET_BITS(7);
}
int cvTst(uint16_t cv) {
	return (cv >> 7) & 1;
}
int cvSOpt(uint16_t cv) {
	return (cv >> 3) & 0xf;
}
int cvSrc(uint16_t cv) {
	return cv & SET_BITS(5);
}
int cvSSpec(uint16_t cv) {
	return cv & SET_BITS(7);
}
int cvImm(uint16_t cv) {
	return cv & 0xff;
}

uint16_t prepend(uint16_t current, int len, int v) {
	return (current << len) | (v & SET_BITS(len));
}
// Construct numeric representation of a source port command from its parts
// dSpec and sSpec combine the option and port
uint16_t makePortCv(int dOpt, int dst, int tst, int sOpt, int src) {
	uint16_t v = 0;
	// Allow the dst and src to include options
	uint16_t dSpec = (dOpt & SET_BITS(4) << 3) | (dst & SET_BITS(7));
	uint16_t sSpec = (sOpt & SET_BITS(4) << 3) | (src & SET_BITS(7));
	v = CMD_TYPE_PORT;
	v = prepend(v, 7, dSpec);
	v = prepend(v, 1, tst);
	v = prepend(v, 7, sSpec);
	return v;
}
// Construct numeric representation of an immediate command from its parts
// dSpec and sSpec combine the option and port
uint16_t makeImmCv(int dOpt, int dst, int value) {
	uint16_t v = 0;
	// Allow the dst to include options
	uint16_t dSpec = ((dOpt & SET_BITS(4)) << 3) | (dst & SET_BITS(7));
	v = CMD_TYPE_IMM;
	v = prepend(v, 7, dSpec);
	v = prepend(v, 8, value);
	return v;
}
/* set the source value for an immediate cmd from a label */
uint16_t labelCv(uint16_t cv, int label) {
	return makeImmCv(cvDOpt(cv), cvDst(cv), label);
}

void print(enum errClass class, const char *msg, ...) {
	static enum errClass baseClass = -1;
	if (class == ERROR)
		exitStatus = EXIT_FAILURE;
	if (class != CONTINUE)
		baseClass = class;
	va_list args;
	if (baseClass == TRACE && !trace)
		return;

	if (class != CONTINUE) {
		if (baseClass != TRACE && parsing)
			fprintf(stderr, "%s:%d:%d: ", fileName, line, col);
		fprintf(stderr, "%s", errClassStr[baseClass]);
	}

	va_start(args, msg);
	vfprintf(stderr, msg, args);
	va_end(args);
	if (baseClass == FATAL)
		exit(EXIT_FAILURE);
}

void printBinary(const char *fmt, int value) {
	int len = strlen(fmt);
	int bit = 1 << len;
	for (int c = 0; fmt[c]; c++) {
		if (fmt[c] == '_') {
			print(CONTINUE, "_%c", value & bit ? '1' : '0');
			bit >>= 1;
		} else if (fmt[c] == 'c') {
			print(CONTINUE, "%c", value & bit ? '1' : '0');
			bit >>= 1;
		} else
			print(FATAL, "Unexpected character, %c, in binary Format.", fmt[c]);
	}
}

void printCmdVal(uint16_t cv) {
	// formats define one bit per character with
	//  _ : print a bit preceeded by an underscore
	//  c : print a bit
	//  else: print the character.
	static const char *bit_format_imm = "c_ccc_cc_ccccccc";
	static const char *bit_format_port = "c_ccc_cc__ccc_cc";
	if (cvIsImm(cv)) {
		printBinary(bit_format_imm, cv);
		print(CONTINUE, "  ");
	} else {
		printBinary(bit_format_port, cv);
	}
}

void printCmd(const struct cmd *c) {
	int optCnt = 0;
	if (!c->isUsed)
		return;
	print(TRACE, "");
	print(CONTINUE, "<0x%4.4x / 0b", c->cv);
	printCmdVal(c->cv);
	print(CONTINUE, ">");
	if (c->label != NULL) {
		print(CONTINUE, "  %s : ", c->label);
	} else
		print(CONTINUE, "    ");
	print(CONTINUE, "%s", c->dstName);
	for (optCnt = 0; optCnt < MAX_OPTIONS && c->dOpt[optCnt]; optCnt++) {
		print(CONTINUE, " , %s", c->dOpt[optCnt]);
	}
	print(CONTINUE, " [0b");
	printBinary("cccc_cc", cvDSpec(c->cv));
	print(CONTINUE, "]");
	if (cvIsImm(c->cv)) {
		print(CONTINUE, "%s %s [%d]", c->assignType, c->srcName, cvImm(c->cv));
	} else {
		print(CONTINUE, "%s %s", c->assignType, c->srcName);
		for (optCnt = 0; optCnt < MAX_OPTIONS && c->sOpt[optCnt]; optCnt++) {
			print(CONTINUE, " , %s", c->sOpt[optCnt]);
		}
		print(CONTINUE, " [0b");
		printBinary("cccc_cc", cvSSpec(c->cv));
		print(CONTINUE, "]");
	}
	if (optCnt > 0)
		print(CONTINUE, " [%d]", cvSOpt(c->cv));
	print(CONTINUE, "\n");
}

bool tryRead(int (*test)(int), int *c) {
	*c = getc(stdin);
	if (!test(*c)) {
		ungetc(*c, stdin);
		return false;
	}
	if (*c == '\n')
		col = 1, line++;
	else
		col++;
	return true;
}

/* intern a string */
const char *intern(char *s, int len) {
	struct strings *next = interned;
	while (next) {
		if (next->len == len && !strncmp(s, next->str, len)) {
			return next->str;
		}
		next = next->next;
	}
	{
		char *newStr = malloc(len + 1);
		struct strings *newEntry = malloc(sizeof(struct strings));
		memcpy(newStr, s, len);
		newStr[len] = '\0';
		newEntry->str = newStr;
		newEntry->len = len;
		newEntry->next = interned;
		interned = newEntry;
		return newEntry->str;
	}
}

// Returns Symbol if id is registered else NULL
Symbol findSymbol(Symbol symList, const char *id) {
	Symbol nxt = symList;
	while (nxt) {
		if (nxt->id == id) {
			;
			return nxt;
		}
		nxt = nxt->next;
	}
	return NULL;
}

Symbol addSymbolToList(Symbol *symList, const char *name, const char *id,
					   int value) {
	Symbol sym = findSymbol(*symList, id);
	if (sym == NULL) {
		sym = malloc(sizeof(struct symbols));
		sym->id = id;
		sym->value = value;
		sym->next = *symList;
		*symList = sym;
		print(TRACE, "added new %s: %s = %d\n", name, id, value);
	}
	return sym;
}
Symbol addSymbol(const char *id, int value) {
	return addSymbolToList(&symbols, "symbol", id, value);
}
Symbol addPort(const char *id, int value) {
	return addSymbolToList(&ports, "port", id, value);
}

int significant(int c) {
	return isgraph(c) || c == '\n' || c == EOF;
}
int insignificant(int c) {
	return !(significant(c));
}
int graphic(int c) {
	return isgraph(c);
}
int lineTerm(int c) {
	return c == '\n' || c == EOF;
}
int notLineTerm(int c) {
	return !(lineTerm(c));
}

void skipInsignificantCharacters() {
	int c;
	while (tryRead(insignificant, &c))
		;
}
// Returns the next interned token.
// A token is a newline, eof or sequence of isgraph() characters.
const char *readToken() {
	char buf[MAX_TOKEN_LENGTH + 1];
	int lastC;
	int len = 0;
	skipInsignificantCharacters();
	if (!tryRead(significant, &lastC)) {
		print(FATAL, "Why can't we read a significant character?");
	}
	if (lastC == '\n')
		return T_NL;
	if (lastC == EOF)
		return T_EOF;
	buf[len++] = lastC;
	while (len < MAX_TOKEN_LENGTH && tryRead(graphic, &lastC))
		buf[len++] = lastC;
	if (len >= MAX_TOKEN_LENGTH) {
		buf[MAX_TOKEN_LENGTH] = '\0';
		print(ERROR, "Length of token, %s", buf);
		while (tryRead(graphic, &lastC))
			print(CONTINUE, "%c", (char)lastC);
		print(CONTINUE, ", exceeds the maximum, %d\n", MAX_TOKEN_LENGTH);
	}
	return intern(buf, len);
}
void skipComment() {
	int lastC;
	while (tryRead(notLineTerm, &lastC))
		continue;
}
bool tokenIsLineTerm(const char *t) {
	return t == T_NL || t == T_EOF;
}
// Get the next word from src as a pointer to an interned string.
// Skip whitespace and comments.
#define readWord() readOrPeekWord(false)
#define peekWord() readOrPeekWord(true)
// Reads a Token but never returns a comment token. Instead it skips comments
// and returns the line termination (NL or EOF). The caller either needs one
// or the caller will ignore as an empty line
const char *readOrPeekWord(bool peek) {
	static const char *pushedWord = NULL;
	const char *word = pushedWord;

	if (word == NULL) {
		word = readToken();
		if (word == T_COMMENT_START) {
			skipComment();
			word = readToken();
			assert(tokenIsLineTerm(word) &&
				   "How did we fail to read a line termination");
		}
	}
	pushedWord = peek ? word : NULL;
	return word;
}

bool skipWordIf(const char *w) {
	if (peekWord() != w)
		return false;
	readWord();
	return true;
}

void expectLineEnd() {
	if (!skipWordIf(T_NL)) {
		print(ERROR, "Expected new line\n");
		for (const char *word = readWord(); word && word != T_NL;
			 word = readWord())
			;
	}
}

void parseExport() {
	const char *state;
	state = readWord();
	if (state != T_ON && state != T_OFF) {
		print(ERROR, "expected on or off for new export state\n");
		return;
	}
	expectLineEnd();
	export = state == T_ON;
	print(TRACE, "export set to %d\n", export);
}

// If id is an identifier set value to its value and return true
bool resolveIdentifier(Symbol symList, const char *id, int *value) {
	Symbol nxt = symList;
	while (nxt) {
		if (nxt->id == id) {
			*value = nxt->value;
			return true;
		}
		nxt = nxt->next;
	}
	*value = -1;
	return false;
}

// Extract a binary integer from a string. Allows _ digit separator, does not
// allow preceding 0b.
// On return, sNxt points to the first character of s that is not consumed.
long binStrToL(const char *str, char **sNxt) {
	long result = 0;
	char *c = (char *)str;
	while (*c == '0' || *c == '1' || *c == '_') {
		if (*c != '_') {
			result = result * 2 + (*c == '0' ? 0 : 1);
		}
		c++;
	}
	*sNxt = c;
	return result;
}

/* Attempts to resolve s as a symbol name and if found returns that symbol's
 *  value.
 *  Otherwise, treats s as the string representation of a hex, binary or decimal
 *  number, parses that number and returns that as the value.
 */
int deriveSymbolValueFromList(Symbol symList, const char *type, const char *s) {
	char *sNxt;
	int result = 0;
	if (resolveIdentifier(symList, s, &result))
		return result;
	if (s[0] == '0' && s[1] == 'x') {
		result = strtol(s, &sNxt, 16);
	} else if (s[0] == '0' && s[1] == 'b') {
		result = binStrToL(&s[2], &sNxt);
	} else {
		result = strtol(s, &sNxt, 10);
	}
	if (sNxt[0] == '\0')
		return result;
	print(ERROR, "error deriving value for %s %s\n", type, s);
	return -1;
}
int deriveSymbolValue(const char *s) {
	return deriveSymbolValueFromList(symbols, "symbol", s);
}
int derivePortValue(const char *s) {
	return deriveSymbolValueFromList(ports, "port", s);
}
void parseDef() {
	const char *id = readWord();
	int value = -1;
	if (tokenIsLineTerm(id)) {
		print(ERROR, "No symbol defined\n");
		return;
	}
	if (tokenIsLineTerm(peekWord())) {
		print(ERROR, "No value for symbol %s\n", id);
	} else {
		const char *valStr = readWord();
		value = deriveSymbolValue(valStr);
	}
	addSymbol(id, value);
	expectLineEnd();
}
void parseSet(bool zeroInit) {
	const char *word;
	int lastVal = zeroInit || symbols == NULL ? 0 : symbols->value + 1;
	for (word = readWord(); word != T_NL && word != NULL; word = readWord()) {
		addSymbol(word, lastVal);
		lastVal++;
	}
}

void parsePort() {
	const char *id = readWord();
	int value = -1;
	if (tokenIsLineTerm(id)) {
		print(ERROR, "No port defined\n");
		return;
	}
	if (tokenIsLineTerm(peekWord()))
		print(ERROR, "No value for port %s\n", id);
	else {
		const char *valStr = readWord();
		value = derivePortValue(valStr);
	}
	addPort(id, value);
	expectLineEnd();
}

void parseForget() {
	const char *forgetTo = readWord();
	int forgetCnt = 0;
	if (tokenIsLineTerm(forgetTo)) {
		forgetTo = NULL;
		print(TRACE, "Forgetting all\n");
	} else {
		expectLineEnd();
	}
	while (symbols != NULL && symbols->id != forgetTo && symbols->id != T_EOF) {
		struct symbols *next = symbols->next;
		free(symbols);
		forgetCnt++;
		symbols = next;
	}
	if (forgetTo != NULL && symbols == NULL) {
		print(ERROR, "Failed to find %s so forgot all %d symbols\n", forgetTo,
			  forgetCnt);
	} else if (symbols == NULL) {
		print(TRACE, "Forgot all %d symbols\n", forgetCnt);
	} else
		print(TRACE, "Forgot %d symbols to %s\n", forgetCnt, forgetTo);
}
void parseCmdSet() {
	static int maxCmdSet = SET_BITS(CMDSET_BITS);
	cmdSet = deriveSymbolValue(readWord());
	print(TRACE, "cmdSet is:%d\n", cmdSet);
	if (cmdSet > maxCmdSet || cmdSet < 0)
		print(ERROR, "Command Set, %d, is out of range 0..%d\n", cmdSet,
			  maxCmdSet);
	expectLineEnd();
}

void parsePage() {
	static int maxPage = SET_BITS(PAGE_BITS);
	page = deriveSymbolValue(readWord());
	print(TRACE, "page is:%d\n", page);
	if (page > maxPage || page < 0)
		print(ERROR, "Page, %d, is out of range 0..%d\n", page, maxPage);
	expectLineEnd();
}
bool peekWordIsAssignment() {
	const char *nxtWord = peekWord();
	return (nxtWord == T_EQ || nxtWord == T_EQ_IMM || nxtWord == T_EQ_LABEL ||
			nxtWord == T_EQ_TEST);
}

// The source and destination are constructed from a sequence of
// words. The first word is assumed to be a port but can also include
// options. It is thus allowed to be 7 bits long. The subsequent words,
// if any, are options. Options are ordered together and will be shifted
// right by 3 bits when the command value is constructed
void parseCmd(struct cmd *c) {
	int dOpt = 0, sOpt = 0, tst = 0, src = 0, dst = 0;
	int optCnt;
	c->isUsed = true;
	c->dstName = readWord(); // assume no label

	if (skipWordIf(T_LABEL_SEP)) {
		c->label = c->dstName;
		c->dstName = readWord();
	}
	dst = derivePortValue(c->dstName);

	for (optCnt = 0, dOpt = 0; !peekWordIsAssignment() && optCnt < MAX_OPTIONS;
		 optCnt++) {
		c->dOpt[optCnt] = readWord();
		dOpt |= derivePortValue(c->dOpt[optCnt]);
	}
	if (!peekWordIsAssignment())
		print(ERROR, "Unexpected assignment type; max options %d\n",
			  MAX_OPTIONS);
	c->assignType = readWord();

	c->srcName = readWord();
	if (c->assignType == T_EQ_IMM) {
		src = deriveSymbolValue(c->srcName);
		c->cv = makeImmCv(dOpt, dst, src);
	} else if (c->assignType == T_EQ_LABEL) {
		c->cv = makeImmCv(dOpt, dst, 0); // resolve src value later
		c->referencedLabel = c->srcName;
	} else {
		src = derivePortValue(c->srcName);
		for (optCnt = 0, sOpt = 0;
			 !tokenIsLineTerm(peekWord()) && optCnt < MAX_OPTIONS; optCnt++) {
			c->sOpt[optCnt] = readWord();
			sOpt |= derivePortValue(c->sOpt[optCnt]);
		}
		if (optCnt > MAX_OPTIONS)
			print(ERROR, "Too many options, max %d\n", MAX_OPTIONS);
		tst = c->assignType == T_EQ_TEST ? CMD_TST : CMD_NO_TST;
		c->cv = makePortCv(dOpt, dst, tst, sOpt, src);
	}
	expectLineEnd();
	return;
}

void parseGrp() {
	static int maxCmdId = SET_BITS(CMD_BITS);
	struct cmd cmds[CMDS_PER_GRP];
	int i = 0;
	int cmdId;
	const char *grpName = readWord();
	memset(cmds, 0, sizeof(cmds));
	if (tokenIsLineTerm(grpName)) {
		print(ERROR, "Expected a cmdGrp id\n");
		return;
	}
	cmdId = deriveSymbolValue(grpName);
	if (cmdId > maxCmdId || cmdId < 0) {
		print(ERROR, "Command Id, %d, is out of range 0..%d\n", cmdId,
			  maxCmdId);
		cmdId = 0;
	}
	if (readWord() != T_CMD_GROUP_START) {
		print(ERROR, "expected { to start a command group\n");
	}
	expectLineEnd();
	print(TRACE, "cmdGrp: %s %d:%d:%d[0x%x]\n", grpName, cmdSet, page, cmdId,
		  MC_ADDR(cmdId));
	for (i = 0; i < CMDS_PER_GRP && peekWord() != T_CMD_GROUP_END; i++) {
		while (skipWordIf(T_NL))
			;
		parseCmd(&cmds[i]);
	}
	if (!skipWordIf(T_CMD_GROUP_END))
		print(ERROR, "expected command group to terminate with }\n");
	for (i = 0; i < CMDS_PER_GRP; i++) {
		int l = 0;
		if (cmds[i].referencedLabel == NULL)
			continue;
		for (; l < CMDS_PER_GRP && cmds[l].label != cmds[i].referencedLabel;
			 l++)
			;
		if (l >= CMDS_PER_GRP)
			print(ERROR, "referenced label, %s, not found\n",
				  cmds[i].referencedLabel);
		else
			cmds[i].cv = labelCv(cmds[i].cv, l);
	}
	expectLineEnd();
	for (i = 0; i < CMDS_PER_GRP; i++)
		printCmd(&cmds[i]);
	fseek(stdout, MC_ADDR(cmdId), 0);
	for (i = 0; i < CMDS_PER_GRP; i++) {
		unsigned char bytes[2];
		bytes[1] = cmds[i].cv >> 16;
		bytes[0] = cmds[i].cv;
		fwrite(bytes, sizeof(bytes[0]), NELEMS(bytes), stdout);
	}
}

void parseStmt(const char *keyWord) {
	if (keyWord == T_GRP)
		parseGrp();
	else if (keyWord == T_DEF)
		parseDef();
	else if (keyWord == T_ZET)
		parseSet(true);
	else if (keyWord == T_SET)
		parseSet(false);
	else if (keyWord == T_PORT)
		parsePort();
	else if (keyWord == T_FORGET)
		parseForget();
	else if (keyWord == T_CMDSET)
		parseCmdSet();
	else if (keyWord == T_PAGE)
		parsePage();
	else if (keyWord == T_EXPORT)
		parseExport();
	else if (keyWord != T_NL)
		print(ERROR, "unexpected statement keyword: %s\n", keyWord);
}

void parseFile(const char *srcFile) {
	fileName = srcFile;
	line = 1;
	col = 1;
	if (srcFile == NULL || freopen(srcFile, "r", stdin) == NULL) {
		print(FATAL, "Can't read %s\n", srcFile);
	}
	parsing = true;
	for (const char *keyWord = readWord(); keyWord != T_EOF;
		 keyWord = readWord())
		parseStmt(keyWord);
	parsing = false;
}

void printHelp(const char *progName) {
	char *usage = "-o <file> [-t] infile [ [-t] infile ...]\n";
	fprintf(stdout, "Usage: %s %s", progName, usage);
}

/*
 *  Opens a microcode output file and pre-fills it with zero.
 *  All microcode files are exactly 64k.
 */
bool openOutputFile(const char *fileName) {
	if (freopen(fileName, "w", stdout) == NULL) {
		print(ERROR, "Can't write to %s\n", fileName);
		return false;
	}
	for (int i = 0; i < 64 * 1024; i++) {
		if (putc(0, stdout) == EOF) {
			print(ERROR, "Couldn't zero fill the output file\n");
			return false;
		}
	}
	return true;
}

int main(int argc, char const *argv[]) {
#define XX(name, str) name = intern(str, strlen(str));
	SYSTEM_TOKENS
#undef XX
	bool outputDefined = false;
	if (argc <= 1)
		printHelp(argv[0]);
	for (int i = 1; i < argc; i++)
		if (strcmp(argv[i], "-t") == 0)
			trace = 1;
		else if (strcmp(argv[i], "-o") == 0 && i + 1 >= argc) {
			print(FATAL, "No output file for output file option, -o\n");
		} else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
			i++;
			outputDefined = openOutputFile(argv[i]);
		} else if (*argv[i] != '-')
			if (!outputDefined) {
				print(ERROR, "No output file defined for input %s\n", argv[i]);
			} else {
				parseFile(argv[i]);
			}
		else
			print(FATAL, "Unknown argument, %s\n", argv[i]);
	return exitStatus;
}
