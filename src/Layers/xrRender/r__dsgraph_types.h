#pragma once

#include "../../xrCore/FixedMap.h"

//#ifndef USE_MEMORY_MONITOR
//#	define USE_DOUG_LEA_ALLOCATOR_FOR_RENDER
//#endif // USE_MEMORY_MONITOR

#ifdef USE_DOUG_LEA_ALLOCATOR_FOR_RENDER
//	extern doug_lea_allocator	g_render_lua_allocator;

	template <class T>
	class doug_lea_alloc {
	public:
		typedef	size_t		size_type;
		typedef ptrdiff_t	difference_type;
		typedef T*			pointer;
		typedef const T*	const_pointer;
		typedef T&			reference;
		typedef const T&	const_reference;
		typedef T			value_type;

	public:
		template<class _Other>	
		struct rebind			{	typedef doug_lea_alloc<_Other> other;	};
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
			typedef doug_lea_alloc<T>	result;
		};

		static	void	*alloc		(const u32 &n)	{	return g_render_lua_allocator.malloc_impl((u32)n);	}
		template <typename T>
		static	void	dealloc		(T *&p)			{	g_render_lua_allocator.free_impl((void*&)p);	}
	};

#	define render_alloc				doug_lea_alloc
	typedef doug_lea_allocator_wrapper	render_allocator;

#else // USE_DOUG_LEA_ALLOCATOR_FOR_RENDER
#	define render_alloc				xalloc
typedef xr_allocator render_allocator;
#endif // USE_DOUG_LEA_ALLOCATOR_FOR_RENDER

class dxRender_Visual;
struct FloraVertData;

// #define	USE_RESOURCE_DEBUGGER

namespace R_dsgraph
{
	// Elementary types
	struct _NormalItem
	{
		float ssa;
		dxRender_Visual* pVisual;
	};

	// Tree instancing item - groups identical trees by CRC+LOD for batch rendering
	// Trees are batched by both geometry (CRC) AND LOD level to prevent flickering
	// when different trees in the same batch would have different LOD levels.
	struct _TreeItem
	{
		dxRender_Visual* pVisual;
		xr_vector<FloraVertData*> data;
	};

	// Helper to create a combined key from CRC and LOD for tree batching
	// Upper 32 bits = CRC (geometry hash), Lower 32 bits = LOD index
	inline u64 make_tree_batch_key(u32 crc, u32 lod)
	{
		return (static_cast<u64>(crc) << 32) | static_cast<u64>(lod);
	}

	struct _MatrixItem
	{
		float ssa;
		IRenderable* pObject;
		dxRender_Visual* pVisual;
		Fmatrix Matrix; // matrix (copy)
		Fmatrix PrevMatrix;
	};

	struct _MatrixItemS : public _MatrixItem
	{
		ShaderElement* se;
	};

	struct _LodItem
	{
		float ssa;
		dxRender_Visual* pVisual;
	};

#ifdef USE_RESOURCE_DEBUGGER
	typedef	ref_vs						vs_type;
	typedef	ref_ps						ps_type;
#	if defined(USE_DX10) || defined(USE_DX11)
		typedef	ref_gs						gs_type;
#		ifdef USE_DX11
		typedef	ref_hs						hs_type;
		typedef	ref_ds						ds_type;
#		endif
#	endif	//	USE_DX10
#else
#if defined(USE_DX10) || defined(USE_DX11)	//	DX10 needs shader signature to propperly bind deometry to shader
		typedef	SVS*					vs_type;
		typedef	ID3DGeometryShader*		gs_type;
#ifdef USE_DX11
			typedef	ID3D11HullShader*		hs_type;
			typedef	ID3D11DomainShader*		ds_type;
#endif
#else	//	USE_DX10
	typedef ID3DVertexShader* vs_type;
#endif	//	USE_DX10
	typedef ID3DPixelShader* ps_type;
#endif

	// NORMAL
	typedef xr_vector<_NormalItem, render_allocator::helper<_NormalItem>::result> mapNormalDirect;

	// mapNormalItems now holds both regular items and trees for GPU instancing
	// Maintains vector-like interface for backwards compatibility with existing code
	// NOTE: trees is a pointer because FixedMAP uses raw memory allocation (ZeroMemory)
	// which doesn't call constructors. xr_unordered_map requires proper construction.
	struct mapNormalItems
	{
		float ssa{};
		mapNormalDirect items;
#ifdef USE_DX11
		// Trees grouped by CRC+LOD key (u64) to ensure consistent LOD within each batch
		// This prevents flickering caused by different trees having different LODs
		xr_unordered_map<u64, _TreeItem>* trees{nullptr};
#endif

		// Forward vector-like methods for backwards compatibility
		void push_back(const _NormalItem& item) { items.push_back(item); }
		auto begin() { return items.begin(); }
		auto end() { return items.end(); }
		auto begin() const { return items.begin(); }
		auto end() const { return items.end(); }
		void clear()
		{
			items.clear();
#ifdef USE_DX11
			if (trees)
			{
				delete trees;
				trees = nullptr;
			}
#endif
		}
		bool empty() const { return items.empty(); }
		size_t size() const { return items.size(); }

#ifdef USE_DX11
		// Get or create the trees map (lazy allocation)
		xr_unordered_map<u64, _TreeItem>& get_trees()
		{
			if (!trees)
				trees = new xr_unordered_map<u64, _TreeItem>();
			return *trees;
		}
#endif
	};

	struct mapNormalTextures : public FixedMAP<STextureList*, mapNormalItems, render_allocator>
	{
		float ssa;
	};

	struct mapNormalStates : public FixedMAP<ID3DState*, mapNormalTextures, render_allocator>
	{
		float ssa;
	};

	struct mapNormalCS : public FixedMAP<R_constant_table*, mapNormalStates, render_allocator>
	{
		float ssa;
	};
#ifdef USE_DX11
	struct	mapNormalAdvStages
	{
		hs_type		hs;
		ds_type		ds;
		mapNormalCS	mapCS;
	};
	struct	mapNormalPS			: public	FixedMAP<ps_type, mapNormalAdvStages,render_allocator>						{	float	ssa;	};
#else
	struct mapNormalPS : public FixedMAP<ps_type, mapNormalCS, render_allocator>
	{
		float ssa;
	};
#endif	//	USE_DX11
#if defined(USE_DX10) || defined(USE_DX11)
	struct	mapNormalGS			: public	FixedMAP<gs_type, mapNormalPS,render_allocator>						{	float	ssa;	};
	struct	mapNormalVS			: public	FixedMAP<vs_type, mapNormalGS,render_allocator>						{	};
#else	//	USE_DX10
	struct mapNormalVS : public FixedMAP<vs_type, mapNormalPS, render_allocator>
	{
	};
#endif	//	USE_DX10
	typedef mapNormalVS mapNormal_T;
	typedef mapNormal_T mapNormalPasses_T[SHADER_PASSES_MAX];

	// MATRIX
	typedef xr_vector<_MatrixItem, render_allocator::helper<_MatrixItem>::result> mapMatrixDirect;

	struct mapMatrixItems : public mapMatrixDirect
	{
		float ssa;
	};

	struct mapMatrixTextures : public FixedMAP<STextureList*, mapMatrixItems, render_allocator>
	{
		float ssa;
	};

	struct mapMatrixStates : public FixedMAP<ID3DState*, mapMatrixTextures, render_allocator>
	{
		float ssa;
	};

	struct mapMatrixCS : public FixedMAP<R_constant_table*, mapMatrixStates, render_allocator>
	{
		float ssa;
	};
#ifdef USE_DX11
	struct	mapMatrixAdvStages
	{
		hs_type		hs;
		ds_type		ds;
		mapMatrixCS	mapCS;
	};
	struct	mapMatrixPS			: public	FixedMAP<ps_type, mapMatrixAdvStages,render_allocator>						{	float	ssa;	};
#else
	struct mapMatrixPS : public FixedMAP<ps_type, mapMatrixCS, render_allocator>
	{
		float ssa;
	};
#endif	//	USE_DX11
#if defined(USE_DX10) || defined(USE_DX11)
	struct	mapMatrixGS			: public	FixedMAP<gs_type, mapMatrixPS,render_allocator>						{	float	ssa;	};
	struct	mapMatrixVS			: public	FixedMAP<vs_type, mapMatrixGS,render_allocator>						{	};
#else	//	USE_DX10
	struct mapMatrixVS : public FixedMAP<vs_type, mapMatrixPS, render_allocator>
	{
	};
#endif	//	USE_DX10
	typedef mapMatrixVS mapMatrix_T;
	typedef mapMatrix_T mapMatrixPasses_T[SHADER_PASSES_MAX];

	// Top level
	typedef FixedMAP<float, _MatrixItemS, render_allocator> mapSorted_T;
	typedef mapSorted_T::TNode mapSorted_Node;

	typedef FixedMAP<float, _MatrixItemS, render_allocator> mapHUD_T;
	typedef mapHUD_T::TNode mapHUD_Node;

#if defined(USE_DX11)
	typedef FixedMAP<float, _MatrixItemS, render_allocator> mapScopeHUD_T; // Redotix99: for 3D Shader Based Scopes
	typedef mapScopeHUD_T::TNode mapScopeHUD_T_Node;
#endif

	typedef FixedMAP<float, _MatrixItemS, render_allocator> HUDMask_T;
	typedef HUDMask_T::TNode HUDMask_Node;

	typedef FixedMAP<float, _LodItem, render_allocator> mapLOD_T;
	typedef mapLOD_T::TNode mapLOD_Node;

	typedef FixedMAP<float, _MatrixItemS, render_allocator> mapLandscape_T;
	typedef mapLandscape_T::TNode mapLandscape_Node;

	typedef FixedMAP<float, _MatrixItemS, render_allocator> mapWater_T;
	typedef mapWater_T::TNode mapWater_Node;

};
