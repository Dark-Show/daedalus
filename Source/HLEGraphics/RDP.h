/*

  Copyright (C) 2002 StrmNrmn

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/


#ifndef RDP_H__
#define RDP_H__

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


inline u32 pixels2bytes( u32 pixels, u32 size )
{
	return ((pixels << size) + 1 ) >> 1;
}

inline u32 bytes2pixels( u32 bytes, u32 size )
{
	return (bytes << 1) >> size;
}

struct SImageDescriptor
{
	u32 Format;	// "RGBA", "YUV", "CI", "IA", "I", "?1", "?2", "?3"
	u32 Size;	// "4bpp", "8bpp", "16bpp", "32bpp"
	u32 Width;	// Num Pixels
	u32 Address;	// Location
	//u32 Bpl; // Width << Size >> 1

	inline u32 GetPitch() const
	{
		DAEDALUS_ASSERT( Size, " No need to compute Pitch" );
		return (Width << Size >> 1);
	}

	//Get Bpl -> ( Width << Size >> 1 )
	inline u32 GetAddress(u32 x, u32 y) const
	{
		return Address + y * ((Width << Size) >> 1) + ((x << Size) >> 1);
	}

	// Optimised version when Size == G_IM_SIZ_16b
	inline u32 GetAddress16bpp(u32 x, u32 y) const
	{
		return Address + ((y * Width + x) << 1);
	}
};

//*****************************************************************************
// Types
//*****************************************************************************
#include "PushStructPack1.h"

// Order here should be the same as in TnLPSP
enum ETnLModeFlags
{
	TNL_LIGHT		= 1 << 0,
	TNL_TEXTURE		= 1 << 1,
	TNL_TEXGEN		= 1 << 2,
	TNL_TEXGENLIN	= 1 << 3,
	TNL_FOG			= 1 << 4,
	TNL_SHADE		= 1 << 5,
	TNL_ZBUFFER		= 1 << 6,
	TNL_TRICULL		= 1 << 7,
	TNL_CULLBACK	= 1 << 8,
};

struct TnLPSP
{
	union
	{
		struct
		{
			u32 Light : 1;			// 0x1
			u32 Texture : 1;		// 0x2
			u32 TexGen : 1;			// 0x4
			u32 TexGenLin : 1;		// 0x8
			u32 Fog : 1;			// 0x10
			u32 Shade : 1;			// 0x20
			u32 Zbuffer : 1;		// 0x40
			u32 TriCull : 1;		// 0x80
			u32 CullBack : 1;		// 0x100
			u32 pad0 : 23;			// 0x0
		};
		u32	_u32;
	};
};

struct RDP_GeometryMode
{
	union
	{
		union
		{
			struct
			{
				u32 GBI1_Zbuffer : 1;		// 0x1
				u32 GBI1_Texture : 1;		// 0x2
				u32 GBI1_Shade : 1;			// 0x4
				u32 GBI1_pad0 : 6;			// 0x0
				u32 GBI1_ShadingSmooth : 1;	// 0x200
				u32 GBI1_pad1 : 2;			// 0x0
				u32 GBI1_CullFront : 1;		// 0x1000
				u32 GBI1_CullBack : 1;		// 0x2000
				u32 GBI1_pad2 : 2;			// 0x0
				u32 GBI1_Fog : 1;			// 0x10000
				u32 GBI1_Lighting : 1;		// 0x20000
				u32 GBI1_TexGen : 1;		// 0x40000
				u32 GBI1_TexGenLin : 1;		// 0x80000
				u32 GBI1_Lod : 1;			// 0x100000
				u32 GBI1_pad3 : 11;			// 0x0
			};
			struct
			{
				u32 GBI2_Zbuffer : 1;		// 0x1
				u32 GBI2_pad0 : 8;			// 0x0
				u32 GBI2_CullBack : 1;		// 0x200
				u32 GBI2_CullFront : 1;		// 0x400
				u32 GBI2_pad1 : 5;			// 0x0
				u32 GBI2_Fog : 1;			// 0x10000
				u32 GBI2_Lighting : 1;		// 0x20000
				u32 GBI2_TexGen : 1;		// 0x40000
				u32 GBI2_TexGenLin : 1;		// 0x80000
				u32 GBI2_Lod : 1;			// 0x100000
				u32 GBI2_ShadingSmooth : 1;	// 0x200000
				u32 GBI2_pad2 : 10;			// 0x0
			};
		};
		u32	_u32;
	};
};

struct RDP_OtherMode
{
	union
	{
		struct
		{
			// Low bits
			u32		alpha_compare : 2;			// 0..1
			u32		depth_source : 1;			// 2..2

		//	u32		render_mode : 13;			// 3..15
			u32		aa_en : 1;					// 3
			u32		z_cmp : 1;					// 4
			u32		z_upd : 1;					// 5
			u32		im_rd : 1;					// 6
			u32		clr_on_cvg : 1;				// 7

			u32		cvg_dst : 2;				// 8..9
			u32		zmode : 2;					// 10..11

			u32		cvg_x_alpha : 1;			// 12
			u32		alpha_cvg_sel : 1;			// 13
			u32		force_bl : 1;				// 14
			u32		tex_edge : 1;				// 15 - Not used

			u32		blender : 16;				// 16..31


			// High bits
			u32		blend_mask : 4;				// 0..3 - not supported
			u32		alpha_dither : 2;			// 4..5
			u32		rgb_dither : 2;				// 6..7

			u32		comb_key : 1;				// 8..8
			u32		text_conv : 3;				// 9..11
			u32		text_filt : 2;				// 12..13
			u32		text_tlut : 2;				// 14..15

			u32		text_lod : 1;				// 16..16
			u32		text_detail : 2;			// 17..18
			u32		text_persp : 1;				// 19..19
			u32		cycle_type : 2;				// 20..21
			u32		color_dither : 1;			// 22..22 - not supported
			u32		pipeline : 1;				// 23..23

			u32		pad : 8;					// 24..31 - padding

		};

		u64			_u64;

		struct
		{
			u32	L;
			u32	H;
		};
	};
};

struct RDP_Combine
{
	union
	{
		struct
		{
			// muxs1
			u32	dA1		: 3;
			u32	bA1		: 3;
			u32	dRGB1	: 3;
			u32	dA0		: 3;
			u32	bA0		: 3;
			u32	dRGB0	: 3;
			u32	cA1		: 3;
			u32	aA1		: 3;
			u32	bRGB1	: 4;
			u32	bRGB0	: 4;

			// muxs0
			u32	cRGB1	: 5;
			u32	aRGB1	: 4;
			u32	cA0		: 3;
			u32	aA0		: 3;
			u32	cRGB0	: 5;
			u32	aRGB0	: 4;
			u32 pad		: 8;
		};

		u64	mux;

		struct
		{
			u32	L;
			u32	H;
		};
	};
};


struct RDP_TexRect
{
	union
	{
		struct
		{
			u32 cmd3;
			u32 cmd2;
			u32 cmd1;
			u32 cmd0;
		};

		struct
		{
			// cmd3
			s32		dtdy : 16;
			s32		dsdx : 16;

			// cmd2
			s32		t : 16;
			s32		s : 16;

			// cmd1
			u32		y0 : 12;
			u32		x0 : 12;
			u32		tile_idx : 3;
			s32		pad1 : 5;

			// cmd0
			u32		y1 : 12;
			u32		x1 : 12;

			u32		cmd : 8;
		};
	};
};

struct RDP_MemRect
{
	union
	{
		struct
		{
			u32 cmd2;
			u32 cmd1;
			u32 cmd0;
		};

		struct
		{

			// cmd2
			s32		t : 16;
			s32		s : 16;

			// cmd1
			u32		pad3 : 2;
			u32		y0 : 10;
			u32		pad2 : 2;
			u32		x0 : 10;
			u32		tile_idx : 3;
			s32		pad1 : 5;

			// cmd0
			u32		pad5 : 2;
			u32		y1 : 10;
			u32		pad4 : 2;
			u32		x1 : 10;

			u32		cmd : 8;
		};

	};
};

struct RDP_Tile
{
	union
	{
		struct
		{
			u32		cmd1;
			u32		cmd0;
		};

		struct
		{
			// cmd1
			u32		shift_s : 4;
			u32		mask_s : 4;
			u32		mirror_s : 1;
			u32		clamp_s : 1;

			u32		shift_t : 4;
			u32		mask_t : 4;
			u32		mirror_t : 1;
			u32		clamp_t : 1;

			u32		palette : 4;
			u32		tile_idx : 3;

			u32		pad1 : 5;

			// cmd0
			u32		tmem : 9;
			u32		line : 9;
			u32		pad0 : 1;
			u32		size : 2;
			u32		format : 3;
			u32		cmd : 8;
		};

	};

	bool	operator==( const RDP_Tile & rhs ) const
	{
		return cmd0 == rhs.cmd0 && cmd1 == rhs.cmd1;
	}
	bool	operator!=( const RDP_Tile & rhs ) const
	{
		return cmd0 != rhs.cmd0 || cmd1 != rhs.cmd1;
	}
};


struct RDP_TileSize
{
	union
	{
		struct
		{
			u32		cmd1;
			u32		cmd0;
		};

		struct
		{
			// cmd1
			u32		bottom : 12;
			u32		right : 12;

			u32		tile_idx : 3;
			u32		pad1 : 5;

			// cmd0
			u32		top : 12;
			u32		left : 12;

			u32		cmd : 8;
		};
	};


	u16	GetWidth() const
	{
		return ( ( right - left ) / 4 ) + 1;
	}

	u16	GetHeight() const
	{
		return ( ( bottom - top ) / 4 ) + 1;
	}

	bool	operator==( const RDP_TileSize & rhs ) const
	{
		return cmd0 == rhs.cmd0 && cmd1 == rhs.cmd1;
	}
	bool	operator!=( const RDP_TileSize & rhs ) const
	{
		return cmd0 != rhs.cmd0 || cmd1 != rhs.cmd1;
	}
};



#include "PopStructPack.h"


//*****************************************************************************
// Externs
//*****************************************************************************
extern RDP_OtherMode		gRDPOtherMode;

#ifdef DAEDALUS_FAST_TMEM
extern u32* gTlutLoadAddresses[ 4096 >> 6 ];
#define TLUT_BASE ((u32)(gTlutLoadAddresses[0]))
#else
extern u16 gTextureMemory[];
#define TLUT_BASE ((u32)(&gTextureMemory[0]))
#endif


#endif // RDP_H__
