/*                                                                           */
/*   DS_UTY.h   効果音関連の処理                                             */
/*                                                                           */
/*                                                                           */

#ifndef PBGWIN_DS_UTY_H
#define PBGWIN_DS_UTY_H		"DS_UTY : Version 0.03 : Update 2000/02/04"


// 更新履歴 //


// ヘッダファイル //
#include "DX_TYPE.H"					// 型＆マクロ
#include "DX_ERROR.H"					// エラー処理用関数
#include "LZ_UTY.H"						// 圧縮管理用


// 定数＆マクロ //
#define SND_OBJ_MAX			30				// 効果音の種類の最大数
#define SND_XMID_DEFAULT	(320*64)		// Ｘ座標の中心のデフォルト値

#ifndef DSBCAPS_CTRLDEFAULT
	#define DSBCAPS_CTRLDEFAULT \
		(DSBCAPS_CTRLPAN|DSBCAPS_CTRLVOLUME|DSBCAPS_CTRLFREQUENCY)
#endif


// 構造体 //

#pragma pack(push, PBG_WAVEHEAD_PACK)
#pragma pack(2)

typedef struct tagWAVEHEAD{
	DWORD	RIFF;					//
	DWORD	RiffLen;				//
	DWORD	WAVE;					//
	DWORD	FMT;					//
	DWORD	WaveLen;				//

	WORD	tag;					//
	WORD	ch;						//
	DWORD	SamplePerSec;			//
	DWORD	AvgBytePerSec;			//
	WORD	BlockAlign;				//
	WORD	BitPerSample;			//

	DWORD	DATA;					//
	DWORD	DataLen;				//
} WAVEHEAD;

typedef struct tagWAVEHEAD_FACT{
	DWORD	RIFF;					//
	DWORD	RiffLen;				//
	DWORD	WAVE;					//
	DWORD	FMT;					//
	DWORD	WaveLen;				//

	WORD	tag;					//
	WORD	ch;						//
	DWORD	SamplePerSec;			//
	DWORD	AvgBytePerSec;			//
	WORD	BlockAlign;				//
	WORD	BitPerSample;			//

	DWORD	FACT;
	WORD	Dummy_2;
	WORD	Dummy_3;
	WORD	Dummy_4;
	WORD	Dummy_5;

	DWORD	DATA;					//
	DWORD	DataLen;				//
} WAVEHEAD_FACT;

#pragma pack(pop, PBG_WAVEHEAD_PACK)


// 初期化など //
FBOOL SndInit(HWND hWnd,int x_mid=SND_XMID_DEFAULT);
FVOID SndCleanup(void);

// ＷＡＶファイルロード(ある制限があるが...) //
FBOOL SndWAVLoad(char *filename,BYTE id,int max);				// 後でエラーを吐くようにする
FBOOL SndWAVLoadP(const PACKFILE_READ &in,DWORD n,BYTE id,int max);

// 再生＆停止 //
FVOID SndPlay(BYTE id);
FVOID SndPlayEX(BYTE id,int x,bool loop);
FVOID SndStop(BYTE id);
FVOID SndStopAll(void);

// 補助 //
FVOID SndRelease(BYTE id);
FBOOL SndRestore(void);


// グローバル変数 //


#endif
