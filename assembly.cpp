#define _CRT_SECURE_NO_WARNINGS			// 偷懒---

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "common.h"

#define BUFSIZE 100
#define STRLEN 20

struct Var{
	int size;	// 变量大小，字节表示
	unsigned int addr;	// 物理地址
	unsigned int val;	// 内容
	char name[STRLEN];
	struct Var *next;
}*var_list;

struct Label{
	char name[STRLEN];
	unsigned int addr;
	struct Label *next;
}*label_list, *delay_list;
enum ITYPE{
	R0,		// ins rd, rs, rt
	R1,		// ins rs, rt
	R2,		// ins rd
	R3,		// ins rd, rt, sa
	R4,		// ins rs
	I0,		// ins rt, rs, imm
	I1,		// ins rt, imm
	I2,		// ins rt, off(base)
	I3,		// ins rt, rs, label
	J,		// ins dest
	P		// 伪指令
};

char *readline(FILE *);
unsigned handle_data(char *);
unsigned handle_text(char *);
void handle_label(char *);
void print_var(struct Var *);
void print_label(struct Label *);
void find_ins(char *, int*, int*);
int find_label(char *, unsigned int *);
int find_var(char *, unsigned int *);
int getReg(char *);
void delay_label(char *);
void handle_delay();
void freemem();
void print_ins(unsigned int*);
int assembly_dos(char *, unsigned int *, unsigned int *, char***);

// 数据段和代码段分开存储
unsigned int data_next;
unsigned int text_next;

int ipos;
unsigned int *insbuf;
char **textlines;
char *blank = " \t,";

struct InsTable{
	char name[STRLEN];
	int op;	// 与func复合
	int type;
}table[] = {
	"add", 0x20, R0, "addi", 0x8, I0, "addu", 0x21, R0,
	"sub", 0x22, R0, "subu", 0x25, I0,
	"mult", 0x18, I1, "multu", 0x19, I1,
	"div", 0x19, I1, "divu", 0x1B, I1,
	"slt", 0x2a, R0, "sltu", 0x2B, R0, "slti", 0xA, I0, "sltiu", 0xB, I0,
	"mfhi", 0x10, I2, "mflo", 0x12, I2,
	"lui", 0xf, I1,
	"and", 0x24, R0, "andi", 0xC, I0,
	"or", 0x25, R0, "ori", 0xD, I0,
	"nor", 0x27, R0,
	"xor", 0x26, R0, "xori", 0xE, I0,
	"sll", 0, R3, "sllv", 4, R0,
	"sra", 0x3, R3, "srav", 0x7, R0,
	"srl", 0x2, R3, "srlv", 0x6, R0,
	"lb", 0x20, I2, "lbu", 0x24, I2, "lw", 0x23, I2,
	"sb", 0x28, I2, "sw", 0x2B, I2,
	"beq", 0x4, I3, "bne", 0x5, I3,
	"j", 0x2, J, "jal", 0x3, J, "jr", 0x8, R4,
	"printw", 0x9, R4,
	"laddr", 0, P
};

// 伪指令：
// laddr rs, var	==>	addi rs, %R0, addr(var)

int assembly(char *path, unsigned int *codemem, unsigned int *datamem, char **lines, int *pnlines){
	char *str = NULL;
	FILE *f;
	int mode = -1;
	unsigned int temp;
	int ndata = 0;		// nins用ipos代替
	fopen_s(&f, path, "r");
	insbuf = codemem;
	textlines = lines;
	if (f == NULL)
		return -1;
	while ((str = readline(f)) != NULL
		&& strncmp(str, "", 2) !=0){
		if (str[0] == '.'){
			if (strncmp(&str[1], "data", 4) == 0)
				mode = 0;
			else if (strncmp(&str[1], "text", 4) == 0)
				mode = 1;
			else
				return -3;
		}
		else if (str[strlen(str) - 1] == ':'){	// 标签
			str[strlen(str) - 1] = 0;	// 去掉冒号
			handle_label(str);
		}
		else{
			if (mode == 0){			// 数据段
				temp=handle_data(str);
				if (data_next >= MEMSIZE)
					return -1;
				datamem[ndata++] = temp;
			}
			else if (mode == 1){	// 代码段
				// 保存str
				strncpy(lines[ipos], str, MAXINS);
				char *c = lines[ipos];
				while (*c){
					if (*c == '\t')		// ListView控件无法正常显示\t
						*c = ' ';
					c++;
				}
				temp=handle_text(str);
				if (text_next >= MEMSIZE)
					return -1;
				codemem[ipos++] = temp;
			}
			else
				return -2;
		}
	}
	handle_delay();
	//print_var(var_list);
	//print_label(label_list);
	//print_ins(insbuf);
	*pnlines = ipos;
	freemem();
	fclose(f);
	return 0;
}

// 去掉空白的行读入
char*
readline(FILE *stream){
	static char buf[BUFSIZE];
	char *str;
	size_t n = BUFSIZE;
	memset(buf, 0, BUFSIZE);
	while ((str = fgets(buf, BUFSIZE, stream)) && *str == '\n');
	if (str == NULL)
		return NULL;
	buf[strlen(str) - 1] = 0;	// 处理换行符
	while (*str == ' '
		|| *str == '\t')
		str++;
	return str;
}

// 为每一个变量分配地址空间
unsigned
handle_data(char *str){
	struct Var *var;
	char *name, *cs, *cv;
	char *buf;

	name = strtok_s(str, blank, &buf);	// 取出变量名
	cs = strtok_s(NULL, blank, &buf);	// 取出变量大小
	cv = strtok_s(NULL, blank, &buf);	// 取出变量初值

	if (cs == NULL){
		printf("Error: Lack of size. Format: [name] [size]");
		exit(-1);
	}

	var = (struct Var *)malloc(sizeof(*var));
	if (var == NULL){
		printf("Error: Out of Memory\n");
		exit(-1);
	}
	if (cv == NULL)
		var->val = 0;
	else
		var->val = atoi(cv);

	strncpy_s(var->name, STRLEN, name, STRLEN);
	if (strncmp(cs, "byte", 4) == 0)
		var->size = 1;
	else if (strncmp(cs, "short", 5) == 0)
		var->size = 2;
	else if (strncmp(cs, "word", 4) == 0)
		var->size = 4;
	else{
		printf("Error: Unexpected size. Only 'byte', 'short', word' are allowed\n");
		exit(-1);
	}
	var->addr = data_next;
	data_next += var->size;
	var->next = var_list;
	var_list = var;
	return var->val;
}

unsigned
handle_text(char *str){
	char *name;
	int op;
	int type;
	unsigned int ins = 0;
	char *s;
	int temp;
	char *buf;

	//printf("text: %s\n", str);
	name = strtok_s(str, blank, &buf);
	find_ins(name, &op, &type);
	if (op == -1){
		printf("Error: Unknown instruction %s\n", name);
		exit(-1);
	}
	else if (type >= R0 && type <= R4){	// R型指令
		ins = op;
		switch (type){
		case R0:
			s = strtok_s(NULL, blank, &buf);
			temp = getReg(s);
			ins |= temp << 11;	// rd
			s = strtok_s(NULL, blank, &buf);
			temp = getReg(s);
			ins |= temp << 21;	// rs
			s = strtok_s(NULL, blank, &buf);
			temp = getReg(s);
			ins |= temp << 16;	// rt
			break;
		case R1:
			s = strtok_s(NULL, blank, &buf);
			temp = getReg(s);
			ins |= temp << 21;	// rs
			s = strtok_s(NULL, blank, &buf);
			temp = getReg(s);
			ins |= temp << 16;	// rt
			break;
		case R2:
			s = strtok_s(NULL, blank, &buf);
			temp = getReg(s);
			ins |= temp << 11;	// rd
			break;
		case R3:
			s = strtok_s(NULL, blank, &buf);
			temp = getReg(s);
			ins |= temp << 11;
			s = strtok_s(NULL, blank, &buf);
			temp = getReg(s);
			ins |= temp << 16;
			s = strtok_s(NULL, blank, &buf);
			temp = atoi(s);
			ins |= temp << 6;	// sa
			break;
		case R4:
			s = strtok_s(NULL, blank, &buf);
			temp = getReg(s);
			ins |= temp << 21;
			break;
		}
	}
	else if (type <= I3){			// I型指令
		char *c;
		unsigned int ad;
		ins |= op << 26;
		switch (type){
		case I0:
			s = strtok_s(NULL, blank, &buf);
			temp = getReg(s);					// 获取寄存器的二进制表示
			ins |= temp << 16;
			s = strtok_s(NULL, blank, &buf);
			temp = getReg(s);
			ins |= temp << 21;
			s = strtok_s(NULL, blank, &buf);	// imm
			temp = atoi(s) & 0xffff;
			ins |= temp;
			break;
		case I1:
			s = strtok_s(NULL, blank, &buf);
			temp = getReg(s);
			ins |= temp << 16;
			s = strtok_s(NULL, blank, &buf);
			temp = atoi(s) & 0xffff;
			ins |= temp;
			break;
		case I2:
			s = strtok_s(NULL, blank, &buf);
			temp = getReg(s);
			ins |= temp << 16;
			s = strtok_s(NULL, blank, &buf);
			for (c = s; *c; c++){
				if (*c == '('
					|| *c == ')')
					*c = ' ';
			}
			s = strtok_s(s, blank, &buf);
			temp = atoi(s) & 0xffff;
			ins |= temp;
			s = strtok_s(NULL, blank, &buf);
			temp = getReg(s);
			ins |= temp << 21;
			break;
		case I3:
			s = strtok_s(NULL, blank, &buf);
			temp = getReg(s);
			ins |= temp << 16;
			s = strtok_s(NULL, blank, &buf);
			temp = getReg(s);
			ins |= temp << 21;
			s = strtok_s(NULL, blank, &buf);
			if ((temp = find_label(s, &ad)) < 0){
				delay_label(s);
				goto out;
			}

			// 修改指令中的标号为对应的地址
			char *t;
			t = strstr(textlines[text_next/4], s);
			sprintf(t, "0x%x", ad);

			temp = ad-text_next - 4;
			ins |= temp & 0xffff;

		}
	}
	else if (type == J){					// J型指令
		unsigned int ad;
		ins |= op << 26;
		s = strtok_s(NULL, blank, &buf);
		temp = find_label(s, &ad);
		if (temp == -1){
			delay_label(s);
			goto out;
		}
		// 修改指令中的标号为对应的地址
		char *t;
		t = strstr(textlines[text_next/4], s);
		sprintf(t, "0x%x", ad);

		temp = ad - text_next - 4;
		temp &= 0x0cffffff;
		ins |= temp;
	}
	else{
		if (strncmp(name, "laddr", STRLEN) == 0){
			int r;
			ins = 0x20000000;
			s = strtok_s(NULL, blank, &buf);
			temp = getReg(s);
			ins |= temp << 16;
			s = strtok_s(NULL, blank, &buf);
			if ((r = find_var(s, (unsigned int*)&temp)) < 0){
				printf("Error: Unkown variable\n");
				exit(-1);
			}
			ins |= temp & 0xffff;
			// 将指令中的变量换成地址
			char *t;
			t = strstr(textlines[ipos], s);
			sprintf(t, "0x%x", temp);
		}
		else{
			printf("Error: Unknown pseudo-instruction\n");
			exit(-1);
		}
	}
out:
	text_next += 4;
	return ins;
	//printf("ins=0x%08x\n", ins);
}

void
print_var(struct Var *vl){
	printf("Variable List:\n");
	printf("name\tsize\taddr\n");
	while (vl != NULL){
		printf("%s\t%d\t0x%08x\n", vl->name, vl->size, vl->addr);
		vl = vl->next;
	}
}

// 线性查找
void
find_ins(char *name, int *po, int *pt){
	int i;
	int length = sizeof(table) / sizeof(struct InsTable);
	for (i = 0; i<length; i++){
		if (strncmp(table[i].name, name, STRLEN) == 0){
			*po = table[i].op;
			*pt = table[i].type;
			return;
		}
	}
	*po = *pt = -1;
	return;

}

int
getReg(char *reg){
	int res = atoi(&reg[2]);
	return res;
}

void
handle_label(char *str){
	struct Label *label;
	//printf("label: %s\n", str);
	label = (struct Label*)malloc(sizeof(*label));
	if (label == NULL){
		printf("Error: Out of memory\n");
		exit(-1);
	}
	strncpy_s(label->name, STRLEN, str, STRLEN);
	label->addr = text_next;
	label->next = label_list;
	label_list = label;
}

void
print_label(struct Label *list){
	printf("Label List:\n");
	printf("name\taddr\n");
	while (list != NULL){
		printf("%s\t0x%08x\n", list->name, list->addr);
		list = list->next;
	}
}

int
find_label(char *str, unsigned int *ap){
	struct Label *l = label_list;
	while (l != NULL){
		if (strncmp(l->name, str, STRLEN) == 0){
			*ap = l->addr;
			return 0;
		}
		l = l->next;
	}
	return -1;
}

int
find_var(char *str, unsigned int *pv){
	struct Var *var = var_list;
	while (var != NULL){
		if (strncmp(var->name, str, STRLEN) == 0){
			*pv = var->addr;
			return 0;
		}
		var = var->next;
	}
	return -1;
}

void
delay_label(char *str){
	struct Label *l;
	l = (struct Label*)malloc(sizeof(*l));
	if (l == NULL){
		printf("Error: Out of memory\n");
		exit(-1);
	}
	strncpy_s(l->name, STRLEN, str, STRLEN);
	l->addr = ipos;
	l->next = delay_list;
	delay_list = l;
}

// 处理搁置的标号
void
handle_delay(){
	struct Label *l = delay_list;
	while (l != NULL){
		int r;
		unsigned int ad;			// 标号地址
		if ((r = find_label(l->name, &ad)) < 0){
			printf("Error: Unknown label %s\n", l->name);
			exit(-1);
		}

		// 修改指令中的标号为对应的地址
		char *t;
		t=strstr(textlines[l->addr], l->name);
		sprintf(t, "0x%x", ad);

		ad = ad - l->addr * 4 - 4;
		insbuf[l->addr] |= ad & 0xffff;
		l = l->next;
	}
}

// 需要处理的内存有：变量，标号，延迟标号
void
freemem(){
	struct Var *v = var_list;
	struct Label *l = label_list;
	void *temp;
	while (v != NULL){
		temp = v->next;
		free(v);
		v = (struct Var*)temp;
	}
	while (l != NULL){
		temp = l->next;
		free(l);
		l = (struct Label*)temp;
	}
	l = delay_list;
	while (l != NULL){
		temp = l->next;
		free(l);
		l = (struct Label*)temp;
	}
}

void
print_ins(unsigned int *buf){
	unsigned int addr;
	printf("addr\tinstruction\n");
	for (addr = 0; addr<text_next; addr += 4){
		printf("0x%08x: 0x%08x\n", addr, buf[addr / 4]);
	}
}