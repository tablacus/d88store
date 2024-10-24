// d88store.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <windows.h>
#include <Shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

#pragma pack(push, 1)

struct D88disk {
	char	disk_name[17];
	char	reserved[9];	
	char	write_protected;
	char	disk_type;
	unsigned int	disk_size;
	unsigned int	offset[164];
};

struct D88sector {
	char	cylinder;
	char	side;
	char	sector;
	char	sector_size;
	short	max_sector;
	char	density;
	char	deleted;
	char	status;
	char	reserved[5];
	short	BytesPerSec;
};

struct D88BPB {
	char	JmpBoot[3];
	char	OEMName[8];
	short	BytesPerSec;
	char	SecPerClus;
	short	RsvdSecCnt;
	char	NumFATs;
	short	RootEntCnt;
	short	TotSec16;
	char	Media;
	short	FATSz16;
	short	SecPerTrk;
	short	NumHeads;
};

struct D88DIR {
	char	FileName[8];
	char	Extention[3];
	char	Attribute;
	char	Reserved[10];
	WORD 	Time;
	WORD 	Date;
	WORD 	Cluster;
	int		FileSize;
};

#pragma pack(pop)

D88BPB bpb_2d = {
	{ 0xEB, 0xFE, 0x90 },
	{ 0x4C, 0x44, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20 },
	512,
	2,
	1,
	2,
	112,
	720,
	0xFD,
	2,
	9,
	2
};

D88disk header;
unsigned char bpb_buffer[sizeof(D88BPB)];
D88BPB *pbpb = (D88BPB *)bpb_buffer;
unsigned char fat_buffer[6144];
D88DIR dir_buffer[512];
int max_clusters;
int dir_pos;
int data_pos;

int	read_abs(HANDLE fh, unsigned char *buf, int de, int h) {
	while (--h >= 0) {
		int track = int(de / pbpb->SecPerTrk);
		int sector = (de % pbpb->SecPerTrk) + 1;
		++de;
		D88sector sector_header;
		SetFilePointer(fh, header.offset[track], NULL, FILE_BEGIN);
		DWORD bytesRead;
		ReadFile(fh, &sector_header, sizeof(D88sector), &bytesRead, NULL);
		for (int i = 0; i < sector_header.max_sector; i++) {
			if (sector == sector_header.sector) {
				break;
			}
			SetFilePointer(fh, sector_header.BytesPerSec, NULL, FILE_CURRENT);
			ReadFile(fh, &sector_header, sizeof(D88sector), &bytesRead, NULL);
		}
		if (sector != sector_header.sector) {
			return 1;
		}
		ReadFile(fh, buf, sector_header.BytesPerSec, &bytesRead, NULL);
		buf += sector_header.BytesPerSec;
	}
	return 0;
}

int	write_abs(HANDLE fh, unsigned char *buf, int de, int h) {
	while (--h >= 0) {
		int track = int(de / pbpb->SecPerTrk);
		int sector = (de % pbpb->SecPerTrk) + 1;
		++de;
		D88sector sector_header;
		SetFilePointer(fh, header.offset[track], NULL, FILE_BEGIN);
		DWORD bytesRead;
		ReadFile(fh, &sector_header, sizeof(D88sector), &bytesRead, NULL);
		for (int i = 0; i < sector_header.max_sector; i++) {
			if (sector == sector_header.sector) {
				break;
			}
			SetFilePointer(fh, sector_header.BytesPerSec, NULL, FILE_CURRENT);
			ReadFile(fh, &sector_header, sizeof(D88sector), &bytesRead, NULL);
		}
		if (sector != sector_header.sector) {
			return 1;
		}
		if (!WriteFile(fh, buf, sector_header.BytesPerSec, &bytesRead, NULL)) {
			return 1;
		}
		buf += sector_header.BytesPerSec;
	}
	return 0;
}

int d88_findfile(D88DIR *pDir) {
	for (int i = 0; i < pbpb->RootEntCnt; i++) {
		D88DIR *pDir1 = &dir_buffer[i];
		if (!pDir1->FileName[0]) {
			return -1;
		}
		if (StrCmpNA(pDir->FileName, pDir1->FileName, 8 + 3) == 0) {
			return i;
		}
	}
	return -1;
}

int d88_findnew() {
	for (int i = 0; i < pbpb->RootEntCnt; i++) {
		if (!dir_buffer[i].FileName[0] || dir_buffer[i].FileName[0] == (char)(0xe5)) {
			return i;
		}
	}
	return -1;
}

int get_fat(int i) {
	int offset = i * 3 / 2;
	if (i & 1) {
		return fat_buffer[offset + 1] << 4 | ((fat_buffer[offset] & 0xf0) >> 4);
	} else {
		return ((fat_buffer[offset + 1] & 0x0f) << 8) | fat_buffer[offset];
	}
}

void set_fat(int i, int n) {
	int offset = i * 3 / 2;
	if (i & 1) {
		int h = n >> 4;
		int l = n & 0x0f;
		fat_buffer[offset + 1] = h;
		fat_buffer[offset] = (fat_buffer[offset] & 0x0f) | (l << 4);
	} else {
		int h = n >> 8;
		int l = n & 0xff;
		fat_buffer[offset + 1] = (fat_buffer[offset + 1] & 0xf0) | h;
		fat_buffer[offset] = l;
	}
}

int find_fat(int i) {
	while (i < max_clusters) {
		if (get_fat(i) == 0) {
			return i;
		}
		i++;
	}
}

int main(int argc, char *argv[]) {
	printf("D88STORE v%s\n", "0.01");
	if (argc < 3) {
		printf("usage:\nd88store <file name> <disk image.d88>\n");
		return EXIT_FAILURE;
	}
	char szPath[MAX_PATH];

	D88DIR dir;

	HANDLE fh= CreateFileA(
		argv[2],                // ファイル名
		GENERIC_READ | GENERIC_WRITE, // 読み取りおよび書き込みアクセス
		FILE_SHARE_READ | FILE_SHARE_WRITE, // 他のプロセスも読み書きできる
		NULL,                    // セキュリティ属性（デフォルト）
		OPEN_EXISTING,           // 既存のファイルを開く（存在しない場合エラー）
		FILE_ATTRIBUTE_NORMAL,   // 通常のファイル属性
		NULL                     // テンプレートファイル（なし）
	);
	if (fh == INVALID_HANDLE_VALUE) {
		return EXIT_FAILURE;
	}
	DWORD bytesRead;
	ReadFile(fh, (char *)&header, sizeof(D88disk), &bytesRead, NULL);
	// ファイルを閉じる
	
	SetFilePointer(fh, header.offset[0] + 16, NULL, FILE_BEGIN);
	ReadFile(fh, bpb_buffer, sizeof(D88BPB), &bytesRead, NULL);

	if (pbpb->BytesPerSec < 1 || pbpb->BytesPerSec > 4) {
		::CopyMemory(bpb_buffer, &bpb_2d, sizeof(bpb_buffer));
	}
	dir_pos = pbpb->RsvdSecCnt + pbpb->FATSz16 * pbpb->NumFATs;
	int dir_size = pbpb->RootEntCnt * 32 / pbpb->BytesPerSec;
	//printf("dir_pos/dir_size %d/%d\n", dir_pos, dir_size);
	data_pos = dir_pos + dir_size - 2 * pbpb->SecPerClus;
	//printf("data_pos %d\n", data_pos);
	max_clusters = (pbpb->TotSec16 - data_pos) / pbpb->SecPerClus;
	//printf("max_clusters %d\n", max_clusters);
	read_abs(fh, fat_buffer, pbpb->RsvdSecCnt, pbpb->FATSz16);
	read_abs(fh, (unsigned char *)dir_buffer, dir_pos, dir_size);

	char *pName = ::PathFindFileNameA(argv[1]);
	char *pExt = ::PathFindExtensionA(pName);
	if (pExt[0] == '.') {
		++pExt;
	}
	::ZeroMemory(&dir, sizeof(D88DIR));
	::FillMemory(&dir, 11, 0x20);
	for (int i = 0; i < 8 && pName[i]; i++) {
		dir.FileName[i] = toupper(pName[i]);
	}
	for (int i = 0; i < 3 && pExt[i]; i++) {
		dir.Extention[i] = toupper(pExt[i]);
	}
	int nIndex = d88_findfile(&dir);
	if (nIndex >= 0) {
		D88DIR *pDir = &dir_buffer[nIndex];
		int nCluster = pDir->Cluster;
		while (nCluster && nCluster < max_clusters) {
			int nNext = get_fat(nCluster);
			set_fat(nCluster, 0);
			nCluster = nNext;
		}
		pDir->FileName[0] = 0xe5;
	} else {
		nIndex = d88_findnew();
	}
	if (nIndex >= 0) {
		D88DIR *pDir = &dir_buffer[nIndex];
		HANDLE fhRead= CreateFileA(
			argv[1],                // ファイル名
			GENERIC_READ, // 読み取りアクセス
			FILE_SHARE_READ | FILE_SHARE_WRITE, // 他のプロセスも読み書きできる
			NULL,                    // セキュリティ属性（デフォルト）
			OPEN_EXISTING,           // 既存のファイルを開く（存在しない場合エラー）
			FILE_ATTRIBUTE_NORMAL,   // 通常のファイル属性
			NULL                     // テンプレートファイル（なし）
		);
		if (fhRead == INVALID_HANDLE_VALUE) {
			CloseHandle(fh);
			return EXIT_FAILURE;
		}
		BY_HANDLE_FILE_INFORMATION fileInfo;
		if (!GetFileInformationByHandle(fhRead, &fileInfo) || fileInfo.nFileSizeHigh) {
			CloseHandle(fhRead);
			CloseHandle(fh);
			return EXIT_FAILURE;
		}
		dir.Attribute = fileInfo.dwFileAttributes;
		FILETIME localTime;
		FileTimeToLocalFileTime(&fileInfo.ftLastWriteTime, &localTime);
		FileTimeToDosDateTime(&localTime, &dir.Date, &dir.Time);
		dir.FileSize = fileInfo.nFileSizeLow;
		if (dir.FileSize) {
			int free = 0;
			for (int i = 2; i < max_clusters; i++) {
				if (get_fat(i) == 0) {
					++free;
				}
			} 
			if (dir.FileSize > free * pbpb->BytesPerSec * pbpb->SecPerClus) {
				CloseHandle(fhRead);
				CloseHandle(fh);
				return EXIT_FAILURE;
			}
			//1クラスタ当たりのバイト数
			int BytesPerClus = pbpb->BytesPerSec * pbpb->SecPerClus;
			//printf("空き容量 %d,%d\n", free, free * BytesPerClus);
			unsigned char data[32768];
			if (BytesPerClus > sizeof(data)) {
				CloseHandle(fhRead);
				CloseHandle(fh);
			}
			int cluster = find_fat(2);
			dir.Cluster = cluster;
			int left = dir.FileSize;
			while (left > 0) {
				int next = (left > BytesPerClus) ? find_fat(cluster + 1) : 0xffff;
				set_fat(cluster, next);
				::ZeroMemory(data, BytesPerClus);
				DWORD bytesRead;
				if (!ReadFile(fhRead, data, BytesPerClus, &bytesRead, NULL)) {
					CloseHandle(fhRead);
					CloseHandle(fh);
					return EXIT_FAILURE;
				}
				if (write_abs(fh, data, data_pos + cluster * pbpb->SecPerClus, pbpb->SecPerClus)) {
					CloseHandle(fhRead);
					CloseHandle(fh);
					return EXIT_FAILURE;
				}
				cluster = next;
				left -= BytesPerClus;
			}
			CloseHandle(fhRead);
		}
		::CopyMemory(pDir, &dir, sizeof(dir));
		char c = dir.FileName[8];
		dir.FileName[8] = NULL;
		printf("Name: %s", &dir.FileName);
		dir.FileName[8] = c;
		printf(".%s\n", &dir.Extention);
		for (int i = 0; i < pbpb->NumFATs; i++) {
			if (write_abs(fh, fat_buffer, pbpb->RsvdSecCnt + i * pbpb->FATSz16, pbpb->FATSz16)) {
				CloseHandle(fh);
				return EXIT_FAILURE;
			}
		}
		if (write_abs(fh, (unsigned char *)dir_buffer, dir_pos, dir_size)) {
			CloseHandle(fh);
			return EXIT_FAILURE;
		}
		printf("Completed.\n", &dir.FileName);
	}
	CloseHandle(fh);

	return EXIT_SUCCESS;
}

