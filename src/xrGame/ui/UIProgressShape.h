#pragma once

#include "UIStatic.h"

class CUIStatic;

class CUIProgressShape : public CUIStatic
{
	friend class CUIXmlInit;
public:
	CUIProgressShape();
	virtual ~CUIProgressShape();
	void SetPos(int pos, int max);
	void SetPos(float pos);
	void SetTextVisible(bool b);

	virtual void Draw();

public:
	float m_stage;

protected:
	bool m_bClockwise;
	u32 m_sectorCount;
	CUIStatic* m_pTexture;      // Foreground texture (sectors drawn with this) - Snowy/CS style
	CUIStatic* m_pBackground;   // Background texture (drawn behind sectors) - Snowy/CS style
	bool m_bUseChildTexture;    // true = use m_pTexture (Snowy/CS style), false = use parent's own texture (OWA style)
	bool m_bText;
	bool m_blend;

	float m_angle_begin;
	float m_angle_end;
};
