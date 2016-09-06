#include "CMonster.h"

#include "Monster/CMonsterReal.h"

#include "../Map/CMap.h"

MonsterMap CMonster::g_mMonster;
std::mutex CMonster::g_mxMonster;

CMonster::CMonster(CMonsterInfo *pMacro, int nX, int nY)
{
	m_pMacro = pMacro;
	m_nX = nX;
	m_nY = nY;
}

CMonster::~CMonster()
{
	while (m_Access.IsBusy()) {
		printf("CMonster::~CMonster: Monster is in use, can't delete! Retrying in 10ms...\n");
		usleep(10000);
	}
}

CMonster* CMonster::CreateMonster(WORD wIndex, int nX, int nY)
{
	CMonsterInfo* pMacro = (CMonsterInfo*) CMacroDB::FindMacro(CMacro::MT_MONSTER, wIndex);
	if (!pMacro) {
		printf(KRED "Wrong monster index: %d\n" KNRM, wIndex);
		return NULL;
	}

	CMonster* pMonster=NULL;

	switch (pMacro->m_byRace)
	{
		default:
			printf(KRED "Unknown monster race (%d).\n" KNRM, pMacro->m_byRace);
			pMonster = new CMonsterReal(pMacro, nX, nY);
			break;
	}

	return pMonster;
}

CMonster* CMonster::Summon(WORD wIndex, int nX, int nY)
{
	CMonster *pMonster = CreateMonster(wIndex, nX, nY);
	if (!pMonster)
		return NULL;

	CMap::Add(pMonster);
	CMonster::Add(pMonster);

	Packet createPacket = pMonster->GenerateCreatePacket(true);
	pMonster->SendPacketInSight(createPacket);
}

Packet CMonster::GenerateCreatePacket(bool bHero)
{
	Packet packet;
	memset(&packet, 0, sizeof(Packet));

	packet.byType = S2C_CREATEMONSTER;

	char *end = CSocket::WritePacket(packet.data, "wdddwddIIsbdsIIb", 
		GetIndex(),
		GetID(),
		GetX(),
		GetY(),
		GetDir(),
		100, //curhp
		100, //maxhp
		GetGState(),
		GetMState(),
		"\0",
		GetRace() | (bHero ? 0x80 : 0),
		0, // gid
		"\0",
		GetGStateEx(),
		GetMStateEx(),
		GetLevel() //level
		); 

	packet.wSize = end - ((char*)&packet);

	return packet;
}

Packet CMonster::GenerateDeletePacket()
{
	Packet packet;
	memset(&packet, 0, sizeof(Packet));

	packet.byType = S2C_REMOVEMONSTER;

	char *end = CSocket::WritePacket(packet.data, "d", m_nID);

	packet.wSize = end - ((char*)&packet);

	return packet;
}

Packet CMonster::GeneratePetPacket()
{
	Packet packet;
	packet.wSize=0;

	return packet;
}

Packet CMonster::GenerateMovePacket(BYTE byType, char byX, char byY, char byZ)
{
	Packet packet;
	memset(&packet, 0, sizeof(Packet));

	packet.byType = (byType & 0x10) ? S2C_MOVEMONSTER_END : S2C_MOVEMONSTER_ON;

	char *end = CSocket::WritePacket(packet.data, "dbbb", m_nID, byX, byY, byType & 1);

	packet.wSize = end - ((char*)&packet);

	return packet;
}


void CMonster::Add(CMonster *pMonster)
{
	g_mxMonster.lock();

	if (g_mMonster.find(pMonster->GetID()) == g_mMonster.end())
		g_mMonster[pMonster->GetID()] = pMonster;
	else
		printf(KRED "CMonster::Add: Same monster ingame.\n" KNRM);

	g_mxMonster.unlock();
}

void CMonster::Remove(CMonster *pMonster)
{
	g_mxMonster.lock();

	MonsterMap::iterator it = g_mMonster.find(pMonster->GetID());
	if (it != g_mMonster.end())
		g_mMonster.erase(it);

	g_mxMonster.unlock();
}

CMonster* CMonster::FindMonster(int nID)
{
	CMonster *pMonster=NULL;

	g_mxMonster.lock();

	if (g_mMonster.find(nID) != g_mMonster.end()) {
		pMonster = g_mMonster[nID];
		pMonster->m_Access.Grant();
	}

	g_mxMonster.unlock();

	return pMonster;
}

CMonster* CMonster::FindMonsterByIndex(WORD wIndex)
{
	CMonster *pMonster=NULL;

	g_mxMonster.lock();

	for (auto& a: g_mMonster) {
		if (a.second->GetIndex() == wIndex) {
			pMonster = a.second;
			pMonster->m_Access.Grant();
			break;
		}
	}

	g_mxMonster.unlock();

	return pMonster;
}

void CMonster::Tick()
{
	DWORD dwTime = GetTickCount();

	char byX = (rand() % 65) - 32;
	char byY = (rand() % 65) - 32;

	Move(byX, byY, MT_WALK);

	printf("CMonster::Tick %d\n", GetIndex());
}

void CMonster::Move(char byX, char byY, BYTE byType)
{
	MapInfo mapInfoCur = CMap::GetMapInfo(m_nX, m_nY);
	MapInfo mapInfoDest = CMap::GetMapInfo(m_nX + byX, m_nY + byY);

	if (!mapInfoCur.equalTile(mapInfoDest)) 
	{
		CMap::Remove(mapInfoCur, this);
		CMap::Add(mapInfoDest, this);
	}

	m_nX += byX;
	m_nY += byY;

	SetDirection(byX, byY);

	Packet createPacket = 	GenerateCreatePacket();
	Packet petPacket =		GeneratePetPacket();
	Packet deletePacket = 	GenerateDeletePacket();
	Packet movePacket =   	GenerateMovePacket(byType, byX, byY);

	for (int i = mapInfoDest.wTileX-1; i <= mapInfoDest.wTileX+1; i++) {
		for (int j = mapInfoDest.wTileY-1; j <= mapInfoDest.wTileY+1; j++) {
			auto pTile = CMap::GetTile(i, j);
			if (pTile)
				pTile->SendMoveAction(this, byX, byY, createPacket, petPacket, deletePacket, movePacket);
		}
	}
}