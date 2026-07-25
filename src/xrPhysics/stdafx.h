#pragma once

#define	MTL_EXPORT_API
#define ENGINE_API
#define DLL_API		
#define ECORE_API
//#include "../xrEngine/stdafx.h"

#include "../xrCore/xrCore.h"

#include "../xrServerEntities/smart_cast.h"
//#include "../xrEngine/pure.h"
//#include "../xrEngine/EngineAPI.h"
//#include "../xrEngine/EventAPI.h"


#include "../xrCDB/xrCDB.h"
#include "../xrSound/Sound.h"
//#include "../xrEngine/IGame_Level.h"

#pragma comment( lib, "xrCore.lib"	)

#include "xrPhysics.h"

#include "../Include/xrAPI/xrAPI.h"
#ifdef	DEBUG
#include "d3d9types.h"
#endif
//IC IGame_Level &GLevel()
//{
//	VERIFY( g_pGameLevel );
//	return *g_pGameLevel;
//}
class CGameMtlLibrary;
IC CGameMtlLibrary& GMLibrary()
{
	VERIFY(PGMLib);
	return *PGMLib;
}
