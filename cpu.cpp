#include <Windows.h>
#include <commdlg.h>
#include "resource.h"
#include <CommCtrl.h>
#include <strsafe.h>
#include "common.h"
#include <process.h>

static OPENFILENAME ofn;

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

VOID MemInitialize();
VOID MemFree();
VOID windowInit(HWND, HWND *);
VOID windowAdjust(HWND *, int, int);
VOID PopFileInitialize(HWND);
BOOL PopFileOpenDlg(HWND, PTSTR, PTSTR);
BOOL PopFileRead(PTSTR);
VOID winprintf(char *, ...);
VOID HandleWM_NOTIFY(LPARAM);

VOID handle_alu(DWORD);
VOID handle_loadstore(DWORD);
VOID handle_branch(DWORD);

// �弶��ˮ�߶�Ӧ���̺߳���
VOID thread_if(PVOID);
VOID thread_id(PVOID);
VOID thread_ex(PVOID);
VOID thread_mem(PVOID);
VOID thread_wb(PVOID);

enum Reg{
	R0, R1, R2, R3, R4, R5,
	R6, R7, R8, R9, R10, R11,
	R12, R13, R14, R15, HI, LO,
	PC,
	NREG
};

#define _szReg(x) __szReg(R##x)
#define __szReg(Rx) TEXT(#Rx)
TCHAR szReg[][30] = {
	_szReg(0), _szReg(1), _szReg(2),
	_szReg(3), _szReg(4), _szReg(5),
	_szReg(6), _szReg(7), _szReg(8),
	_szReg(9), _szReg(10), _szReg(11),
	_szReg(12), _szReg(13), _szReg(14),
	_szReg(15),
	TEXT("HI"), TEXT("LO"), TEXT("PC")
};
#undef __szReg
#undef _szReg

enum PREG{
	IFID, IDEX, EXMEM, MEMWB, NPREG,
	IR=0, NPC=1, ALUOUT=1, B=2, LMD=2,
	A=3, COND=3, IMM=4, OPCODE=4,
};

#define SNPREG 5	// ÿһ����ˮ�߼Ĵ������ӼĴ�����Ԥ������

TCHAR szPreg[NPREG][SNPREG][30] = {
	{
		TEXT("IR"), TEXT("NPC"), TEXT(""), TEXT(""), TEXT("")
	},	// IFID
	{
		TEXT("IR"), TEXT("NPC"), TEXT("B"), TEXT("A"), TEXT("Imm")
	},	// IDEX
	{
		TEXT("IR"), TEXT("ALUOutput"), TEXT("B"), TEXT("Cond"), TEXT("opcode")
	},	// EXMEM
	{
		TEXT("IR"), TEXT("ALUOutput"), TEXT("LMD"), TEXT(""), TEXT("")
		// MEMWB
	}
};


#define BUFSIZE  200
#define NWINDOW 7	// �Ӵ��ڸ���
#define GAP 10	// ���ڼ��(����)

typedef unsigned long long QWORD;
typedef struct PARAMS{
	HANDLE hEvent;
	DWORD data[3];
} PARAMS, *PPARAMS;

DWORD *regs;
DWORD *pipeRegs[NPREG];
DWORD *codemem;
DWORD *datamem;
char **codelines;
int nlines;
HWND hwndList[NWINDOW];	// �Ӵ��ھ�����Ĵ���1������ˮ�߼Ĵ���4�����洢��2��
int transData[NPREG];	// �̼߳�����ݹ�����

enum Label{
	REGNAME, CONTENT, ADDR, INS
};

TCHAR szLabel[][30] = {
	TEXT("�Ĵ�������"), TEXT("����"),	// �Ĵ�������
	TEXT("��ַ"), TEXT("ָ��")	// �洢������
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	PSTR szCmdLine, int iCmdShow){
	static TCHAR szAppName[] = TEXT("NK-CPU");
	HWND hwnd;
	HMENU hMenu;
	MSG msg;
	WNDCLASS wndclass;
	HACCEL	hAccel;

	wndclass.style = CS_HREDRAW | CS_VREDRAW;
	wndclass.lpfnWndProc = WndProc;
	wndclass.cbClsExtra = 0;
	wndclass.cbWndExtra = 0;
	wndclass.hInstance = hInstance;
	wndclass.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDB_NK));
	wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndclass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wndclass.lpszMenuName = NULL;
	wndclass.lpszClassName = szAppName;
	

	if (!RegisterClass(&wndclass)){
		MessageBox(NULL, TEXT("Fail to Register wndclass"), szAppName, MB_ICONERROR);
		return 0;
	}

	hMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDR_MENU));
	
	hwnd = CreateWindow(szAppName, szAppName, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, hMenu, hInstance, NULL);

	ShowWindow(hwnd, iCmdShow);
	UpdateWindow(hwnd);
	
	hAccel=LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_ACCELERATOR));

	while (GetMessage(&msg, NULL, 0, 0)){
		if (!TranslateAccelerator(hwnd, hAccel, &msg)){
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam){
	static TCHAR szFileName[MAX_PATH], szTitleName[MAX_PATH];
	HINSTANCE hInstance;
	static PARAMS param;
	RECT rect;
	int i, cxClient, cyClient;


	switch (message){
	case WM_CREATE:
		PopFileInitialize(hwnd);	// ��ʼ�����ļ���Ϣ
		MemInitialize();	// ��ʼ���ڴ�
		hInstance = (HINSTANCE)GetWindowLong(hwnd, GWL_HINSTANCE);
		GetClientRect(hwnd, &rect);
		cxClient = rect.right;
		cyClient = rect.bottom;

		for (i = 0; i < NWINDOW; i++){
			hwndList[i] = CreateWindow(WC_LISTVIEW, NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT, 5*i, 0, 5, 5, hwnd, (HMENU)i,
				hInstance, NULL);
		}
		windowAdjust(hwndList, cxClient, cyClient);		// �����Ӵ��ڵ�λ��
		windowInit(hwnd, hwndList);						// ��ʼ��������ʾ

		// ����ͬ�������߳�
		param.hEvent = CreateEvent(0, FALSE, FALSE, NULL);
		_beginthread(thread_wb, 0, &param);
		return 0;
	case WM_SIZE:
		cxClient = LOWORD(lParam);
		cyClient = HIWORD(lParam);
		windowAdjust(hwndList, cxClient, cyClient);
		return 0;
	case WM_COMMAND:
		switch (LOWORD(wParam)){
		case IDM_EXIT:
			PostMessage(hwnd, WM_CLOSE, wParam, lParam);
			return 0;
		case IDM_OPEN:
			if (PopFileOpenDlg(hwnd, szFileName, szTitleName)){
				if (!PopFileRead(szFileName)){
					MessageBox(hwnd, TEXT("Error in assemblying file"), TEXT("Error"), MB_OK | MB_ICONEXCLAMATION);
				}
				ListView_RedrawItems(hwndList[5], 0, nlines);
				ListView_RedrawItems(hwndList[6], 0, MEMSIZE / 4);
			}
			break;
		case IDM_EXCLK:
			SetEvent(param.hEvent);
			return 0;
		}
		break;
	case WM_NOTIFY:
		HandleWM_NOTIFY(lParam);
		return 0;
	case WM_CLOSE:
		MemFree();
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hwnd, message, wParam, lParam);
}

VOID PopFileInitialize(HWND hwnd){
	static TCHAR szFilter[] = TEXT("ASM Files (*.ASM)\0*.asm\0")  \
		TEXT("All Files (*.*)\0*.*\0\0");

	ofn.lStructSize = sizeof (OPENFILENAME);
	ofn.hwndOwner = hwnd;
	ofn.hInstance = NULL;
	ofn.lpstrFilter = szFilter;
	ofn.lpstrCustomFilter = NULL;
	ofn.nMaxCustFilter = 0;
	ofn.nFilterIndex = 0;
	ofn.lpstrFile = NULL;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = MAX_PATH;
	ofn.lpstrInitialDir = NULL;
	ofn.lpstrTitle = NULL;
	ofn.Flags = 0;
	ofn.nFileOffset = 0;
	ofn.nFileExtension = 0;
	ofn.lpstrDefExt = TEXT("asm");
	ofn.lCustData = 0L;
	ofn.lpfnHook = NULL;
	ofn.lpTemplateName = NULL;
}

BOOL PopFileOpenDlg(HWND hwnd, PTSTR pstrFileName, PTSTR pstrTitleName){
	ofn.hwndOwner = hwnd;
	ofn.lpstrFile = pstrFileName;
	ofn.lpstrFileTitle = pstrTitleName;
	ofn.Flags = OFN_HIDEREADONLY | OFN_CREATEPROMPT;

	return GetOpenFileName(&ofn);
}

extern int assembly(char *, unsigned int *, unsigned int *, char **, int *);

BOOL PopFileRead(PTSTR pstrFileName){
	char buffer[MAX_PATH];
	WideCharToMultiByte(CP_ACP, 0, (PWSTR)pstrFileName, -1, buffer, MAX_PATH, NULL, NULL);	// buffer�����ļ�·��
	return !assembly(buffer, (unsigned int *)codemem, (unsigned int*)datamem, codelines, &nlines);		// 1����ɹ���0����ʧ��
}

VOID MemInitialize(){
	// ��ʼ���Ĵ���
	regs = (DWORD*)malloc(NREG*sizeof(DWORD));
	memset(regs, 0, NREG*sizeof(DWORD));
	// ��ʼ���洢��
	datamem = (DWORD*)malloc(MEMSIZE);
	memset(datamem, 0, MEMSIZE);
	codemem = (DWORD*)malloc(MEMSIZE);
	memset(codemem, 0, MEMSIZE);
	// ��ʼ����ˮ�߼Ĵ���
	int i;
	for (i = 0; i < NPREG; i++){
		pipeRegs[i] = (DWORD*)malloc(SNPREG*sizeof(DWORD));
		memset(pipeRegs[i], 0, SNPREG*sizeof(DWORD));
	}
	// ��ʼ�����뻺����
	codelines = (char **)malloc(MEMSIZE / 4 * sizeof(char *));
	for (i = 0; i < MEMSIZE / 4; i++){
		codelines[i] = (char *)malloc(MAXINS*sizeof(char));
		memset(codelines[i], 0, MAXINS*sizeof(char));
	}
}

VOID MemFree(){
	free(regs);
	regs = NULL;
	free(datamem);
	datamem = NULL;
	free(codemem);
	codemem = NULL;
	int i;
	for (i = 0; i < NPREG; i++){
		free(pipeRegs[i]);
		pipeRegs[i] = NULL;
	}
	for (i = 0; i < MAXINS / 4; i++){
		free(codelines[i]);
	}
	free(codelines);
	codelines = NULL;
}

VOID winprintf(char * fmt, ...){
	va_list ap;
	TCHAR wbuf[BUFSIZE] = { 0 };
	TCHAR wfmt[BUFSIZE] = { 0 };
	MultiByteToWideChar(CP_ACP, 0, fmt, BUFSIZE, wfmt, BUFSIZE);
	va_start(ap, fmt);
	wvsprintf(wbuf, wfmt, ap);
	MessageBox(NULL, wbuf, TEXT("Debug"), MB_OK);
	va_end(ap);
}

VOID windowInit(HWND phwnd, HWND *hwndList){
	int i,j;

	for (i = 0; i < NWINDOW; i++){
		SendMessage(hwndList[i], WM_SETREDRAW, FALSE, 0);	// �رմ����ػ�
	}

	LVCOLUMN lvc;
	RECT rect;
	int cxWindow;
	// 1. ��ӷ���
	memset(&lvc, 0, sizeof(LVCOLUMN));
	lvc.mask = LVCF_TEXT | LVCF_WIDTH;
	// 1.1 �Ĵ�����
	for (i = 0; i < 5; i++){
		GetWindowRect(hwndList[i], &rect);
		cxWindow = rect.right - rect.left;

		lvc.cx = cxWindow / 2;

		lvc.pszText = szLabel[REGNAME];
		ListView_InsertColumn(hwndList[i], 0, &lvc);
		lvc.pszText = szLabel[CONTENT];
		ListView_InsertColumn(hwndList[i], 1, &lvc);
	}
	// 1.2 �洢����
	GetWindowRect(hwndList[5], &rect);
	cxWindow = rect.right - rect.left;
	lvc.cx = cxWindow / 3;
	lvc.pszText = szLabel[ADDR];
	ListView_InsertColumn(hwndList[5], 0, &lvc);
	lvc.pszText = szLabel[CONTENT];
	ListView_InsertColumn(hwndList[5], 1, &lvc);
	lvc.pszText = szLabel[INS];
	ListView_InsertColumn(hwndList[5], 2, &lvc);

	GetWindowRect(hwndList[6], &rect);
	cxWindow = rect.right - rect.left;
	lvc.cx = cxWindow / 2;
	lvc.pszText = szLabel[ADDR];
	ListView_InsertColumn(hwndList[6], 0, &lvc);
	lvc.pszText = szLabel[CONTENT];
	ListView_InsertColumn(hwndList[6], 1, &lvc);

	// 2. ����������
	for (i = 0; i < NWINDOW; i++){
		ListView_SetExtendedListViewStyle(hwndList[i], LVS_EX_GRIDLINES);
	}

	// 3. ��������
	LVITEM lvi;
	memset(&lvi, 0, sizeof(LVITEM));
	lvi.mask = LVIF_TEXT;
	// 3.1 ��ͨ�Ĵ�����
	for (i = 0; i < NREG; i++){
		lvi.iSubItem = 0;
		lvi.iItem = i;
		lvi.pszText = szReg[i];
		ListView_InsertItem(hwndList[0], &lvi);
		lvi.iSubItem = 1;
		lvi.pszText = LPSTR_TEXTCALLBACK;
		ListView_SetItem(hwndList[0], &lvi);
	}
	// 3.2 ��ˮ�߼Ĵ�����
	int N[NPREG] = { 2, 5, 5, 3 };	// ʵ�ʵ��ӼĴ�������
	for (i = 0; i < NPREG; i++){	// ����ÿһ����ˮ�߼Ĵ���
		for (j = 0; j < N[i]; j++){	// ����ÿһ���ӼĴ���
			lvi.iSubItem = 0;
			lvi.iItem = j;
			lvi.pszText = szPreg[i][j];
			ListView_InsertItem(hwndList[1 + i], &lvi);
			lvi.iSubItem = 1;
			lvi.pszText = LPSTR_TEXTCALLBACK;
			ListView_SetItem(hwndList[1 + i], &lvi);
		}
	}
	// 3.3 ָ��洢��
	// ÿ4���ֽ�ռһ��
	TCHAR buf[BUFSIZE];
	for (i = 0; i < MEMSIZE / 4; i++){
		lvi.iSubItem = 0;
		lvi.iItem = i;
		wsprintf(buf, TEXT("0x%x"), i * 4);
		lvi.pszText = buf;
		ListView_InsertItem(hwndList[5], &lvi);		// ��ַ
		lvi.iSubItem = 1;
		lvi.pszText = LPSTR_TEXTCALLBACK;
		ListView_SetItem(hwndList[5], &lvi);		// ����
		lvi.iSubItem = 2;
		ListView_SetItem(hwndList[5], &lvi);		// ָ��
	}

	// 3.4 ���ݴ洢��
	for (i = 0; i < MEMSIZE/4; i++){
		lvi.iSubItem = 0;
		lvi.iItem = i;
		wsprintf(buf, TEXT("0x%x"), i*4);
		lvi.pszText = buf;
		ListView_InsertItem(hwndList[6], &lvi);
		lvi.iSubItem = 1;
		lvi.pszText = LPSTR_TEXTCALLBACK;
		ListView_SetItem(hwndList[6], &lvi);
	}

	for (i = 0; i < NWINDOW; i++){
		SendMessage(hwndList[i], WM_SETREDRAW, TRUE, 0);	// �򿪴����ػ�
	}
}

VOID windowAdjust(HWND *hwndList, int cxClient, int cyClient){
	int i, x, y, nwidth, nheight;
	y = 0;
	nwidth = (cxClient - GAP * 4) / 5; nheight = (cyClient - GAP) / 3;
	for (i = 0; i < 5; i++){
		x = (GAP + nwidth)*i;
		MoveWindow(hwndList[i], x, y, nwidth, nheight, FALSE);	// �Ĵ�����
	}
	x = 0;
	y = GAP + nheight;
	nwidth = (cxClient - GAP) / 5 * 3;
	nheight = cyClient - GAP - nheight;
	MoveWindow(hwndList[5], x, y, nwidth, nheight, false);	// ָ��洢��
	x += GAP + nwidth;
	nwidth = cxClient - GAP - nwidth;
	MoveWindow(hwndList[6], x, y, nwidth, nheight, false);	// ���ݴ洢��
}

VOID HandleWM_NOTIFY(LPARAM lParam){
	NMLVDISPINFO* plvdi;
	TCHAR buf[BUFSIZE];
	memset(buf, 0, BUFSIZE);

	switch (((LPNMHDR)lParam)->code){
	case LVN_GETDISPINFO:
		plvdi = (NMLVDISPINFO *)lParam;
		switch (plvdi->hdr.idFrom){
		case 0:	// ��ͨ�Ĵ���
			wsprintf(buf, TEXT("0x%x"), regs[plvdi->item.iItem]);
			break;
		case 1:	// IFID
			wsprintf(buf, TEXT("0x%x"), pipeRegs[IFID][plvdi->item.iItem]);
			break;
		case 2:	// IDEX
			wsprintf(buf, TEXT("0x%x"), pipeRegs[IDEX][plvdi->item.iItem]);
			break;
		case 3:	// EXMEM
			wsprintf(buf, TEXT("0x%x"), pipeRegs[EXMEM][plvdi->item.iItem]);
			break;
		case 4:	// MEMWB
			wsprintf(buf, TEXT("0x%x"), pipeRegs[MEMWB][plvdi->item.iItem]);
			break;
		case 5:	// ָ��洢��
			if (plvdi->item.iSubItem == 1)		// ����
				wsprintf(buf, TEXT("0x%x"), codemem[plvdi->item.iItem]);
			else								// ָ��
			if (plvdi->item.iItem < nlines){		// �Ѷ����ָ�Χ��
				MultiByteToWideChar(CP_ACP, 0, codelines[plvdi->item.iItem], strnlen_s(codelines[plvdi->item.iItem], BUFSIZE), buf, BUFSIZE);
				//StringCchCopy(plvdi->item.pszText, plvdi->item.cchTextMax, buf);
				//wsprintf(buf, TEXT("addi %R6, %R0, 20"));
			}
			break;
		case 6:	// ���ݴ洢��
			wsprintf(buf, TEXT("0x%x"), datamem[plvdi->item.iItem]);
			break;
		}
		
		plvdi->item.pszText = buf;
		break;	
	}
}

// ��ȡָ������ֵĺ�
#define _RS(ins) ((ins>>21)&0x1f)
#define _RT(ins) ((ins>>16)&0x1f)
#define _RD(ins) ((ins>>11)&0x1f)
#define _IMM(ins) (ins&0xffff)
#define _SIGNEX(data) (data&0x8000 ? data|0xffff0000 : data)
#define _OP(ins) (ins>>26)
#define _FUNC(ins) (ins&0x3f)
#define _SA(ins) ((ins>>6)&0x1f)
#define _ISLOAD(ins) ((_OP(ins)>>3) == 4)
#define _ISSTORE(ins) ((_OP(ins)>>3)==5)


VOID thread_if(PVOID pvoid){
	PPARAMS pparam = (PPARAMS)pvoid; 
	while (1){
		WaitForSingleObject(pparam->hEvent, INFINITE);

		pipeRegs[IFID][IR] = codemem[regs[PC]/4];	// ȡָ
		if (pipeRegs[EXMEM][OPCODE] == 2 && pipeRegs[EXMEM][COND]){	// �����һ��ָ������תָ�����ת��������
			transData[IFID] = 1;								// ����ex��һ��ָ���Ǵ����ָ��
			regs[PC] = pipeRegs[EXMEM][ALUOUT];					// PCΪ��ת��ַ
		}
		else{
			transData[IFID] = 0;
			regs[PC] += 4;										// ��������һ����ַ
		}
		pipeRegs[IFID][NPC] = regs[PC];						// NPC���ڼ�����תָ��ĵ�ַ

		ListView_RedrawItems(hwndList[1], 0, 1);
		ListView_RedrawItems(hwndList[0], PC, PC);
	}
}

VOID thread_id(PVOID pvoid){
	PPARAMS pparam = (PPARAMS)pvoid;
	static PARAMS param;
	param.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);	/*���ƺ����̵߳�ͬ����*/
	_beginthread(thread_if, 0, &param);	/*���������߳�*/
	while (1){
		WaitForSingleObject(pparam->hEvent, INFINITE);
		if (pipeRegs[IFID][IR] == 0 ||
			transData[IFID]){
			int i;
			for (i = IR; i <= IMM; i++){
				pipeRegs[IDEX][i] = 0;
			}
			goto end;
		}
		
		DWORD ins, ins_prev;
		ins = pipeRegs[IFID][IR]; ins_prev = pipeRegs[IDEX][IR];
		int stall = 0;
		// ��ͻ���
		if (_ISLOAD(ins_prev)){	// �����һ��ָ����Load
			if (_OP(ins) == 0 &&
				_FUNC(ins) !=8){	// �����ǰָ���ǼĴ���ALU
				if (_RT(ins_prev) == _RS(ins)
					|| _RT(ins_prev) == _RT(ins))
					stall = 1;
			}
			else if (		// ��Ҫ�õ�rs��ָ��
				_OP(ins) !=3 &&	// ����jal
				_OP(ins)!=2	&&	// ����j
				_OP(ins)!=0xf	// ����lui
				){
				if (_RT(ins_prev)==_RS(ins))
					stall = 1;
			}
		}
		if (stall){
			int i;
			for (i = IR; i <= IMM; i++){
				pipeRegs[IDEX][i] = 0;
			}
			ListView_RedrawItems(hwndList[2], IR, IMM);
			continue;
		}

		pipeRegs[IDEX][A] = regs[_RS(ins)];	// ALU����
		pipeRegs[IDEX][B] = regs[_RT(ins)];	// ALU����
		pipeRegs[IDEX][NPC] = pipeRegs[IFID][NPC];
		pipeRegs[IDEX][IR] = pipeRegs[IFID][IR];
		pipeRegs[IDEX][IMM] = _SIGNEX(_IMM(pipeRegs[IFID][IR]));

end:
		ListView_RedrawItems(hwndList[2], IR, IMM);
		SetEvent(param.hEvent);
	}
}

VOID thread_ex(PVOID pvoid){
	PPARAMS pparam = (PPARAMS)pvoid;
	static PARAMS param;
	param.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);	/*���ƺ����̵߳�ͬ����*/
	_beginthread(thread_id, 0, &param);	/*���������߳�*/
	int type = -1;	// ָ������
	while (1){
		WaitForSingleObject(pparam->hEvent, INFINITE);
		if (pipeRegs[IDEX][IR] == 0){// ������תָ�ID����ʧЧ
			int i;
			for (i = IR; i <= OPCODE; i++)
				pipeRegs[EXMEM][i] = 0;
			goto end;
		}
		if (transData[IFID])
			goto end;

		// ��·����
		DWORD ins, ins_prev, ins_2prev;
		DWORD in_rs = pipeRegs[IDEX][A], in_rt = pipeRegs[IDEX][B];	// ALU��Ĭ������
		ins = pipeRegs[IDEX][IR]; ins_prev = pipeRegs[EXMEM][IR]; ins_2prev = pparam->data[IR];
		// ���������ָ�����������
		if (ins_2prev){
			if ((_OP(ins_2prev) == 0
				&& _FUNC(ins_2prev)!=8)){	// ����������ǼĴ���ALU
				if (_RD(ins_2prev) == _RS(ins))
					in_rs = pparam->data[ALUOUT];		// ALUOUT
				if (((_OP(ins) == 0	// ֻ�е�ǰָ���ǼĴ���ALU����beq��bne����Ҫrt
					&& _FUNC(ins) != 8)
					|| _OP(ins) == 4 || _OP(ins)==5)
					&& _RD(ins_2prev) == _RT(ins))
					in_rt = pparam->data[ALUOUT];
			}
			else if (_OP(ins_2prev) >> 3 == 1){	// ������������ALU
				if (_RT(ins_2prev) == _RS(ins))
					in_rs = pparam->data[ALUOUT];		// ALUOUT
				if (((_OP(ins) == 0	// ֻ�е�ǰָ���ǼĴ���ALU����beq��bne����Ҫrt
					&& _FUNC(ins) != 8)
					|| _OP(ins) == 4 || _OP(ins) == 5)
					&& _RT(ins_2prev) == _RT(ins))
					in_rt = pparam->data[ALUOUT];
			}
			else if (_ISLOAD(ins_2prev)){				// ��������Load
				if (_RT(ins_2prev) == _RS(ins)){
					in_rs = pparam->data[LMD];
				}
				if (((_OP(ins) == 0	// ֻ�е�ǰָ���ǼĴ���ALU����beq��bne����Ҫrt
					&& _FUNC(ins) != 8)
					|| _OP(ins) == 4 || _OP(ins) == 5)
					&& _RT(ins_2prev) == _RT(ins))
					in_rt = pparam->data[LMD];
			}
		}
		// �������ָ�����������
		if (ins_prev){
			if (_OP(ins_prev) == 0
				&& _FUNC(ins_prev) != 8){	// ����������ǼĴ���ALU
				if (_RD(ins_prev) == _RS(ins))
					in_rs = pipeRegs[EXMEM][ALUOUT];		// ALUOUT
				if (((_OP(ins) == 0	// ֻ�е�ǰָ���ǼĴ���ALU����beq��bne����Ҫrt
					&& _FUNC(ins) != 8)
					|| _OP(ins) == 4 || _OP(ins) == 5)
					&& _RD(ins_prev) == _RT(ins))
					in_rt = pipeRegs[EXMEM][ALUOUT];
			}
			else if (_OP(ins_prev) >> 3 == 1){	// ������������ALU
				if (_RT(ins_prev) == _RS(ins))
					in_rs = pipeRegs[EXMEM][ALUOUT];		// ALUOUT
				if (((_OP(ins) == 0	// ֻ�е�ǰָ���ǼĴ���ALU����beq��bne����Ҫrt
					&& _FUNC(ins) != 8)
					|| _OP(ins) == 4 || _OP(ins) == 5)
					&& _RT(ins_prev) == _RT(ins))
					in_rt = pipeRegs[EXMEM][ALUOUT];
			}
		}
		pipeRegs[IDEX][A] = in_rs;
		pipeRegs[IDEX][B] = in_rt;

		int op, func;
		pipeRegs[EXMEM][IR] = pipeRegs[IDEX][IR];
		op = _OP(pipeRegs[EXMEM][IR]);
		func = _FUNC(pipeRegs[EXMEM][IR]);

		// �ж�ָ�������, 0����ALU��1����Load-Store��2����branch
		if (op == 0){
			if (func == 0x08)	// jr
				type = 2;
			else
				type = 0;
		}
		else switch (op>>3){	// ȡǰ3λ
		case 0:
			type = 2;
			break;
		case 1:
			type = 0;
			break;
		case 4:
		case 5:
			type = 1;
			break;
		default:
			goto error;
		}
		//winprintf("ex: type=%d, op=0x%x, func=0x%x", type, op, func);
		switch (type){
		case 0:
			handle_alu(pipeRegs[EXMEM][IR]);
			break;
		case 1:
			handle_loadstore(pipeRegs[EXMEM][IR]);
			break;
		case 2:
			handle_branch(pipeRegs[EXMEM][IR]);
			break;
		}
		pipeRegs[EXMEM][OPCODE] = type;	// ��ifʹ��
		transData[EXMEM] = type;				// ��memʹ��
end:
		ListView_RedrawItems(hwndList[3], IR, OPCODE);
		SetEvent(param.hEvent);
	}

	_endthread();
error:
	winprintf("Error in ex");
	exit(-1);
}

VOID thread_mem(PVOID pvoid){
	PPARAMS pparam = (PPARAMS)pvoid;
	static PARAMS param;
	param.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);	/*���ƺ����̵߳�ͬ����*/
	_beginthread(thread_ex, 0, &param);	/*���������߳�*/
	while (1){
		WaitForSingleObject(pparam->hEvent, INFINITE);

		param.data[IR] = pipeRegs[MEMWB][IR];	// �������ݣ�������·����
		param.data[ALUOUT] = pipeRegs[MEMWB][ALUOUT];
		param.data[LMD] = pipeRegs[MEMWB][LMD];

		if (pipeRegs[EXMEM][IR] == 0){
			int i;
			for (i = IR; i <= LMD; i++)
				pipeRegs[MEMWB][i] = 0;
			ListView_RedrawItems(hwndList[4], IR, LMD);
			SetEvent(param.hEvent);
			continue;
		}

		pipeRegs[MEMWB][IR] = pipeRegs[EXMEM][IR];
		transData[MEMWB]=transData[EXMEM];		// ָ�����ͣ���wbʹ��
		switch (transData[EXMEM]){
		case 0:		// ALU��
			pipeRegs[MEMWB][ALUOUT] = pipeRegs[EXMEM][ALUOUT];
			break;
		case 1:		// Load-Store��
			if ((_OP(pipeRegs[MEMWB][IR]) >> 3) == 4){		// Load
				transData[MEMWB] += 2;
				pipeRegs[MEMWB][LMD] = datamem[pipeRegs[EXMEM][ALUOUT]/4];
			}
			else											// store
				datamem[pipeRegs[EXMEM][ALUOUT]/4] = pipeRegs[EXMEM][B];
			break;
		}

		ListView_RedrawItems(hwndList[4], IR, LMD);
		ListView_RedrawItems(hwndList[6], pipeRegs[EXMEM][ALUOUT/4], pipeRegs[EXMEM][ALUOUT/4]);
		SetEvent(param.hEvent);
	}
}

VOID thread_wb(PVOID pvoid){
	PPARAMS pparam = (PPARAMS)pvoid;
	static PARAMS param;
	param.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	_beginthread(thread_mem, 0, &param);
	while (1){
		WaitForSingleObject(pparam->hEvent, INFINITE);
		if (pipeRegs[MEMWB][IR] == 0){
			SetEvent(param.hEvent);
			continue;
		}
		int op = _OP(pipeRegs[MEMWB][IR]), func = _FUNC(pipeRegs[MEMWB][IR]);
		//winprintf("wb: type=%d", pparam->data);
		switch (transData[MEMWB]){
		case 0:		// ALU��
			if (op == 0 && (func >> 2) != 6)	// ���˳˳��������R��ָ�����rd
				regs[_RD(pipeRegs[MEMWB][IR])] = pipeRegs[MEMWB][ALUOUT];
			else
				regs[_RT(pipeRegs[MEMWB][IR])] = pipeRegs[MEMWB][ALUOUT];
			break;
		case 3:		// Load��
			regs[_RT(pipeRegs[MEMWB][IR])] = pipeRegs[MEMWB][LMD];
			break;
		}
		
		ListView_RedrawItems(hwndList[0], R0, PC);
		SetEvent(param.hEvent);
	}
}

VOID handle_alu(DWORD ins){
	DWORD res, a, b, imm;
	a = pipeRegs[IDEX][A];
	b = pipeRegs[IDEX][B];
	imm = pipeRegs[IDEX][IMM];
	pipeRegs[EXMEM][IR] = pipeRegs[IDEX][IR];
	if (_OP(ins) == 0){
		switch (_FUNC(ins)){
		case 0x20:	// add
			res = (int)a+(int)b;
			break;
		case 0x21:	// addu
			res = a + b;
			break;
		case 0x22:	// sub
			res = (int)a - (int)b;
			break;
		case 0x41:	// subu
			res = a - b;
			break;
		case 0x18:	// mult
				{
					QWORD temp;
					temp = (int)a*(int)b;
					regs[HI] = temp >> 32;
					regs[LO] = temp;
				}
			break;
		case 0x19:	// multu
				{
						QWORD temp;
						temp = a*b;
						regs[HI] = temp >> 32;
						regs[LO] = temp;
				}
			break;
		case 0x1a:	// div
			regs[HI] = (int)a % (int)b;
			regs[LO] = (int)a / (int)b;
			break;
		case 0x1b:	// divu
			regs[HI] = a % b;
			regs[LO] = a / b;
			break;
		case 0x2a:	// slt
			if ((int)a < (int)b)
				res = 1;
			else
				res = 0;
			break;
		case 0x2b:	// sltu
			if (a < b)
				res = 1;
			else
				res = 0;
			break;
		case 0x10:	// mfhi
			res = regs[HI];
			break;
		case 0x12:	// mflo
			res = regs[LO];
			break;
		case 0x24:	// and
			res = a&b;
			break;
		case 0x25:	// or
			res = a | b;
			break;
		case 0x27:	// nor
			res = ~(a^b);
			break;
		case 0x26:	// xor
			res = a^b;
			break;
		case 0x0:	// sll
			res = b << _SA(ins);
			break;
		case 0x4:	// sllv
			res = b << a;
			break;
		case 0x3:	// sra
			if ((int)b < 0){	// ������λ��1
				res = b >> _SA(ins);
				int temp = _SA(ins);
				int i=0;
				while (temp > 0){
					res |= (0x80000000>>i);
					i++;
					temp--;
				}
				break;
			}					// �����Ч���߼�����
			/* fall through */
		case 0x2:	// srl
			res = b >> _SA(ins);
			break;
		case 0x7:	// srav
			if ((int)b < 0){	// ������λ��1
				res = b >> a;
				int temp = a;
				int i = 0;
				while (temp > 0){
					res |= (0x80000000 >> i);
					i++;
					temp--;
				}
				break;
			}					// �����Ч���߼�����
			/* fall through */
		case 0x6:	// srlv
			res = b >> a;
			break;
		}
	}
	else{		// �����Ǻ�����������ָ��
		switch (_OP(ins)){
		case 0x08:	// addi
			res = a + imm;
			break;
		case 0x0a:	// slti
			if ((int)a < imm)
				res = 1;
			else
				res = 0;
			break;
		case 0x0b:	// sltui
			if (a < imm)
				res = 1;
			else
				res = 0;
			break;
		case 0x0f:	// lui
			res = imm << 16;
			break;
		case 0x0c:	// andi
			res = a&imm;
			break;
		case 0x0d:	// ori
			res = a | imm;
			break;
		case 0x0e:	// xori
			res = a^imm;
			break;
		}
	}

	pipeRegs[EXMEM][ALUOUT] = res;
}

VOID handle_loadstore(DWORD ins){
	pipeRegs[EXMEM][IR] = pipeRegs[IDEX][IR];
	pipeRegs[EXMEM][ALUOUT] = pipeRegs[IDEX][A] + pipeRegs[IDEX][IMM];
	pipeRegs[EXMEM][B] = pipeRegs[IDEX][B];
}

VOID handle_branch(DWORD ins){
	pipeRegs[EXMEM][ALUOUT] = pipeRegs[IDEX][NPC] + pipeRegs[IDEX][IMM];
	int op = _OP(ins);
	int a = pipeRegs[IDEX][A], b = pipeRegs[IDEX][B];
	int cond;
	if (op == 0x04){	// beq
		if (a == b)
			cond = 1;
		else
			cond = 0;
	}
	else if (op == 0x05){	// bne
		if (a != b)
			cond = 1;
		else
			cond = 0;
	}
	else
		cond = 1;		// ������������תָ��
	pipeRegs[EXMEM][COND] = cond;
}