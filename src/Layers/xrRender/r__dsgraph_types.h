#pragma once

#include "../../xrCore/fixedmap.h"

//#ifndef USE_MEMORY_MONITOR
//#	define USE_DOUG_LEA_ALLOCATOR_FOR_RENDER
//#endif // USE_MEMORY_MONITOR

#ifdef USE_DOUG_LEA_ALLOCATOR_FOR_RENDER
//	extern doug_lea_allocator	g_render_lua_allocator;

	template <class T>
	class doug_lea_alloc {
	public:
		using size_type = size_t;
		using difference_type = ptrdiff_t;
		using pointer = T*;
		using const_pointer = const T*;
		using reference = T&;
		using const_reference = const T&;
		using value_type = T;

	public:
		template<class _Other>	
		struct rebind			{ using other = doug_lea_alloc<_Other>;	};
	public:
								pointer					address			(reference _Val) const					{	return (&_Val);	}
								const_pointer			address			(const_reference _Val) const			{	return (&_Val);	}
														doug_lea_alloc	()										{	}
														doug_lea_alloc	(const doug_lea_alloc<T>&)				{	}
		template<class _Other>							doug_lea_alloc	(const doug_lea_alloc<_Other>&)			{	}
		template<class _Other>	doug_lea_alloc<T>&		operator=		(const doug_lea_alloc<_Other>&)			{	return (*this);	}
								pointer					allocate		(size_type n, const void* p=0) const	{	return (T*)g_render_lua_allocator.malloc_impl(sizeof(T)*(u32)n);	}
								void					deallocate		(pointer p, size_type n) const			{	g_render_lua_allocator.free_impl	((void*&)p);				}
								void					deallocate		(void* p, size_type n) const			{	g_render_lua_allocator.free_impl	(p);				}
								char*					__charalloc		(size_type n)							{	return (char*)allocate(n); }
								void					construct		(pointer p, const T& _Val)				{	std::_Construct(p, _Val);	}
								void					destroy			(pointer p)								{	std::_Destroy(p);			}
								size_type				max_size		() const								{	size_type _Count = (size_type)(-1) / sizeof (T);	return (0 < _Count ? _Count : 1);	}
	};

	template<class _Ty,	class _Other>	inline	bool operator==(const doug_lea_alloc<_Ty>&, const doug_lea_alloc<_Other>&)		{	return (true);							}
	template<class _Ty, class _Other>	inline	bool operator!=(const doug_lea_alloc<_Ty>&, const doug_lea_alloc<_Other>&)		{	return (false);							}

	struct doug_lea_allocator_wrapper {
		template <typename T>
		struct helper {
			using result = doug_lea_alloc<T>;
		};

		static	void	*alloc		(const u32 &n)	{	return g_render_lua_allocator.malloc_impl((u32)n);	}
		template <typename T>
		static	void	dealloc		(T *&p)			{	g_render_lua_allocator.free_impl((void*&)p);	}
	};

#	define render_alloc				doug_lea_alloc
	using render_allocator = doug_lea_allocator_wrapper;

#else // USE_DOUG_LEA_ALLOCATOR_FOR_RENDER
#	define render_alloc				xalloc
using render_allocator = xr_allocator;
#endif // USE_DOUG_LEA_ALLOCATOR_FOR_RENDER

class dxRender_Visual;

// #define	USE_RESOURCE_DEBUGGER

namespace R_dsgraph
{
	struct DSGraphItem
	{
		float ssa = 0.0f;
		IRenderable* pObject = nullptr;
		dxRender_Visual* pVisual = nullptr;
		Fmatrix* pMatrix = nullptr;
		ShaderElement* pSE = nullptr;
		bool b_hud_mode = false;
	};

#ifdef USE_RESOURCE_DEBUGGER
		using vs_type = ref_vs;
		using ps_type = ref_ps;
#	if defined(USE_DX10) || defined(USE_DX11)
		using gs_type = ref_gs;
#		ifdef USE_DX11
		using hs_type = ref_hs;
		using ds_type = ref_ds;
#		endif
#	endif	//	USE_DX10
#else
#if defined(USE_DX10) || defined(USE_DX11)	//	DX10 needs shader signature to propperly bind deometry to shader
		using vs_type = SVS*;
		using gs_type = ID3DGeometryShader*;
#ifdef USE_DX11
			using hs_type = ID3D11HullShader*;
			using ds_type = ID3D11DomainShader*;
#endif
#else	//	USE_DX10
	using vs_type = ID3DVertexShader*;
#endif	//	USE_DX10
	using ps_type = ID3DPixelShader*;
#endif

	using mapDSGraphItems = xr_vector<DSGraphItem, render_allocator::helper<DSGraphItem>::result>;
	using mapDSGraphTextures = FixedMAP<STextureList*,mapDSGraphItems,render_allocator>;
	using mapDSGraphStates = FixedMAP<ID3DState*,mapDSGraphTextures,render_allocator>;
	using mapDSGraphCS = FixedMAP<R_constant_table*,mapDSGraphStates,render_allocator>;
#ifdef USE_DX11
	struct mapDSGraphAdvStages
	{
		hs_type hs;
		ds_type ds;
		mapDSGraphCS mapCS;
	};
	using mapDSGraphPS = FixedMAP<ps_type, mapDSGraphAdvStages,render_allocator>;
#else
	using mapDSGraphPS = FixedMAP<ps_type, mapDSGraphCS,render_allocator>;
#endif
#if defined(USE_DX10) || defined(USE_DX11)
	using mapDSGraphGS = FixedMAP<gs_type, mapDSGraphPS,render_allocator>;
	using mapDSGraphVS = FixedMAP<vs_type, mapDSGraphGS,render_allocator>;
#else //USE_DX10
	using mapDSGraphVS = FixedMAP<vs_type, mapDSGraphPS,render_allocator>;
#endif
	using mapDSGraphPasses = mapDSGraphVS[SHADER_PASSES_MAX];

	// demonized: fix this to use vectors
	struct _MatrixItem
	{
		float ssa;
		IRenderable* pObject;
		dxRender_Visual* pVisual;
		Fmatrix Matrix;				// matrix (copy)
		Fmatrix PrevMatrix;
	};
	struct _MatrixItemS : public _MatrixItem
	{
		ShaderElement* se;
	};
	using mapSorted_T = FixedMAP<float, _MatrixItemS, render_allocator>;
	using mapSorted_Node = mapSorted_T::TNode;
	using mapWater_T = FixedMAP<float, _MatrixItemS, render_allocator>;
	using mapWater_Node = mapWater_T::TNode;

	struct DynamicSceneRgraph
	{
		struct mapSorted
		{
			mapDSGraphItems Sorted;

			mapDSGraphItems Wmark;
			mapDSGraphItems Emissive;
			mapDSGraphItems Distort;
			mapDSGraphItems ScopeLens;
		};

		mapDSGraphPasses mapStaticPasses[2];	// 2==(priority/2)
		mapSorted mapStaticSorted;

		mapDSGraphPasses mapDynamicPasses[2]; // 2==(priority/2)
		mapSorted mapDynamicSorted;

		mapDSGraphItems mapHUD;
		mapSorted mapHUDSorted;

		mapDSGraphItems mapLOD;

		// Anomaly
		mapDSGraphItems mapScopeHUD;
		mapDSGraphItems mapCamAttached;
		mapWater_T mapWater;
		mapSorted mapScopeHUDSorted;
		mapSorted mapCamAttachedSorted;

		IC void clear_graph(mapDSGraphPasses* graph, u32 _priority)
		{
			PROF_EVENT("r_dsgraph_clear_graph");
			for (u32 iPass = 0; iPass < SHADER_PASSES_MAX; ++iPass)
			{
				mapDSGraphVS& vs = graph[_priority][iPass];
				for (mapDSGraphVS::TNode& Nvs : vs)
				{
	#if defined(USE_DX10) || defined(USE_DX11)
					mapDSGraphGS& gs = Nvs.val;
					for (mapDSGraphGS::TNode& Ngs : gs)
					{
						mapDSGraphPS& ps = Ngs.val;
	#else //USE_DX10
						mapDSGraphPS& ps = Nvs.val;
	#endif
						for (mapDSGraphPS::TNode& Nps : ps)
						{
	#ifdef USE_DX11
							mapDSGraphCS& cs = Nps.val.mapCS;
	#else
							mapDSGraphCS& cs = Nps.val;
	#endif
							for (mapDSGraphCS::TNode& Ncs : cs)
							{
								mapDSGraphStates& states = Ncs.val;
								for (mapDSGraphStates::TNode& Nstate : states)
								{
									mapDSGraphTextures& tex = Nstate.val;
									for (mapDSGraphTextures::TNode& Ntex : tex)
									{
										Ntex.val.clear();
									}
									tex.clear();
								}
								states.clear();
							}
							cs.clear();
						}
						ps.clear();
	#if defined(USE_DX10) || defined(USE_DX11)
					}
					gs.clear();
	#endif //USE_DX10
				}
				vs.clear();
			}
		}
		IC void clear_dynamic()
		{
			clear_graph(mapDynamicPasses, 0);
			clear_graph(mapDynamicPasses, 1);

			mapDynamicSorted.Wmark.clear();
			mapDynamicSorted.Emissive.clear();
			mapDynamicSorted.Sorted.clear();
			mapDynamicSorted.Distort.clear();
		}
		IC void clear_static()
		{
			clear_graph(mapStaticPasses, 0);
			clear_graph(mapStaticPasses, 1);

			mapStaticSorted.Wmark.clear();
			mapStaticSorted.Emissive.clear();
			mapStaticSorted.Sorted.clear();
			mapStaticSorted.Distort.clear();
		}

		IC void clear_hud()
		{
			mapHUD.clear();
			mapHUDSorted.Wmark.clear();
			mapHUDSorted.Emissive.clear();
			mapHUDSorted.Sorted.clear();
			mapHUDSorted.Distort.clear();
		}

		IC void clear_lods()
		{
			mapLOD.clear();
		}

		IC void clear()
		{
			clear_dynamic();
			clear_static();
			clear_hud();
			clear_lods();
		}
	};
};
