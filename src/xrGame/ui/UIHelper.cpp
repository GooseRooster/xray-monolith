////////////////////////////////////////////////////////////////////////////
//	Module 		: UIHelper.cpp
//	Created 	: 17.01.2008
//	Author		: Evgeniy Sokolov
//	Description : UI Helper class implementation
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "UIHelper.h"
#include "UIXmlInit.h"

#include "UIProgressBar.h"
#include "UIFrameLineWnd.h"
#include "UIFrameWindow.h"
#include "UI3tButton.h"
#include "UICheckButton.h"
#include "UIHint.h"
#include "UIDragDropReferenceList.h"
#include "UIEditBox.h"

CUIStatic* UIHelper::CreateStatic(CUIXml& xml, LPCSTR ui_path, CUIWindow* parent)
{
	CUIStatic* ui = xr_new<CUIStatic>();
	if (parent)
	{
		parent->AttachChild(ui);
		// Don't set AutoDelete until after successful initialization
		// DetachChild will auto-delete if AutoDelete is true, causing double-free
	}
	if (!CUIXmlInit::TryInitStatic(xml, ui_path, 0, ui))
	{
		if (parent)
			parent->DetachChild(ui);
		xr_delete(ui);
		return nullptr;
	}
	// Only set AutoDelete after successful initialization
	if (parent)
		ui->SetAutoDelete(true);
	return ui;
}

CUITextWnd* UIHelper::CreateTextWnd(CUIXml& xml, LPCSTR ui_path, CUIWindow* parent)
{
	CUITextWnd* ui = xr_new<CUITextWnd>();
	if (parent)
	{
		parent->AttachChild(ui);
	}
	if (!CUIXmlInit::TryInitTextWnd(xml, ui_path, 0, ui))
	{
		if (parent)
			parent->DetachChild(ui);
		xr_delete(ui);
		return nullptr;
	}
	if (parent)
		ui->SetAutoDelete(true);
	return ui;
}

CUIEditBox* UIHelper::CreateEditBox(CUIXml& xml, LPCSTR ui_path, CUIWindow* parent)
{
	CUIEditBox* ui = xr_new<CUIEditBox>();
	if (parent)
	{
		parent->AttachChild(ui);
	}
	if (!CUIXmlInit::TryInitEditBox(xml, ui_path, 0, ui))
	{
		if (parent)
			parent->DetachChild(ui);
		xr_delete(ui);
		return nullptr;
	}
	if (parent)
		ui->SetAutoDelete(true);
	return ui;
}

CUIProgressBar* UIHelper::CreateProgressBar(CUIXml& xml, LPCSTR ui_path, CUIWindow* parent)
{
	CUIProgressBar* ui = xr_new<CUIProgressBar>();
	if (parent)
	{
		parent->AttachChild(ui);
	}
	if (!CUIXmlInit::TryInitProgressBar(xml, ui_path, 0, ui))
	{
		if (parent)
			parent->DetachChild(ui);
		xr_delete(ui);
		return nullptr;
	}
	if (parent)
		ui->SetAutoDelete(true);
	return ui;
}

CUIFrameLineWnd* UIHelper::CreateFrameLine(CUIXml& xml, LPCSTR ui_path, CUIWindow* parent)
{
	CUIFrameLineWnd* ui = xr_new<CUIFrameLineWnd>();
	if (parent)
	{
		parent->AttachChild(ui);
	}
	if (!CUIXmlInit::TryInitFrameLine(xml, ui_path, 0, ui))
	{
		if (parent)
			parent->DetachChild(ui);
		xr_delete(ui);
		return nullptr;
	}
	if (parent)
		ui->SetAutoDelete(true);
	return ui;
}

CUIFrameWindow* UIHelper::CreateFrameWindow(CUIXml& xml, LPCSTR ui_path, CUIWindow* parent)
{
	CUIFrameWindow* ui = xr_new<CUIFrameWindow>();
	if (parent)
	{
		parent->AttachChild(ui);
	}
	if (!CUIXmlInit::TryInitFrameWindow(xml, ui_path, 0, ui))
	{
		if (parent)
			parent->DetachChild(ui);
		xr_delete(ui);
		return nullptr;
	}
	if (parent)
		ui->SetAutoDelete(true);
	return ui;
}

CUI3tButton* UIHelper::Create3tButton(CUIXml& xml, LPCSTR ui_path, CUIWindow* parent)
{
	CUI3tButton* ui = xr_new<CUI3tButton>();
	if (parent)
	{
		parent->AttachChild(ui);
	}
	if (!CUIXmlInit::TryInit3tButton(xml, ui_path, 0, ui))
	{
		if (parent)
			parent->DetachChild(ui);
		xr_delete(ui);
		return nullptr;
	}
	if (parent)
		ui->SetAutoDelete(true);
	return ui;
}

CUICheckButton* UIHelper::CreateCheck(CUIXml& xml, LPCSTR ui_path, CUIWindow* parent)
{
	CUICheckButton* ui = xr_new<CUICheckButton>();
	if (parent)
	{
		parent->AttachChild(ui);
	}
	if (!CUIXmlInit::TryInitCheck(xml, ui_path, 0, ui))
	{
		if (parent)
			parent->DetachChild(ui);
		xr_delete(ui);
		return nullptr;
	}
	if (parent)
		ui->SetAutoDelete(true);
	return ui;
}

UIHint* UIHelper::CreateHint(CUIXml& xml, LPCSTR ui_path)
{
	UIHint* ui = xr_new<UIHint>();
	ui->SetAutoDelete(true);
	ui->init_from_xml(xml, ui_path);
	return ui;
}

CUIDragDropListEx* UIHelper::CreateDragDropListEx(CUIXml& xml, LPCSTR ui_path, CUIWindow* parent)
{
	CUIDragDropListEx* ui = xr_new<CUIDragDropListEx>();
	if (parent)
	{
		parent->AttachChild(ui);
	}
	if (!CUIXmlInit::TryInitDragDropListEx(xml, ui_path, 0, ui))
	{
		if (parent)
			parent->DetachChild(ui);
		xr_delete(ui);
		return nullptr;
	}
	if (parent)
		ui->SetAutoDelete(true);
	return ui;
}

CUIDragDropReferenceList* UIHelper::CreateDragDropReferenceList(CUIXml& xml, LPCSTR ui_path, CUIWindow* parent)
{
	CUIDragDropReferenceList* ui = xr_new<CUIDragDropReferenceList>();
	if (parent)
	{
		parent->AttachChild(ui);
	}
	if (!CUIXmlInit::TryInitDragDropListEx(xml, ui_path, 0, ui))
	{
		if (parent)
			parent->DetachChild(ui);
		xr_delete(ui);
		return nullptr;
	}
	if (parent)
		ui->SetAutoDelete(true);
	return ui;
}

//////////////////////////////////////////////////////////////////////////
// Optional creation functions - return nullptr if XML node not found
//////////////////////////////////////////////////////////////////////////

CUIStatic* UIHelper::CreateStaticOptional(CUIXml& xml, LPCSTR ui_path, CUIWindow* parent)
{
	CUIStatic* ui = xr_new<CUIStatic>();
	if (parent)
	{
		parent->AttachChild(ui);
	}
	if (!CUIXmlInit::TryInitStatic(xml, ui_path, 0, ui))
	{
		if (parent)
			parent->DetachChild(ui);
		xr_delete(ui);
		return nullptr;
	}
	if (parent)
		ui->SetAutoDelete(true);
	return ui;
}

CUITextWnd* UIHelper::CreateTextWndOptional(CUIXml& xml, LPCSTR ui_path, CUIWindow* parent)
{
	CUITextWnd* ui = xr_new<CUITextWnd>();
	if (parent)
	{
		parent->AttachChild(ui);
	}
	if (!CUIXmlInit::TryInitTextWnd(xml, ui_path, 0, ui))
	{
		if (parent)
			parent->DetachChild(ui);
		xr_delete(ui);
		return nullptr;
	}
	if (parent)
		ui->SetAutoDelete(true);
	return ui;
}

CUIProgressBar* UIHelper::CreateProgressBarOptional(CUIXml& xml, LPCSTR ui_path, CUIWindow* parent)
{
	CUIProgressBar* ui = xr_new<CUIProgressBar>();
	if (parent)
	{
		parent->AttachChild(ui);
	}
	if (!CUIXmlInit::TryInitProgressBar(xml, ui_path, 0, ui))
	{
		if (parent)
			parent->DetachChild(ui);
		xr_delete(ui);
		return nullptr;
	}
	if (parent)
		ui->SetAutoDelete(true);
	return ui;
}

CUI3tButton* UIHelper::Create3tButtonOptional(CUIXml& xml, LPCSTR ui_path, CUIWindow* parent)
{
	CUI3tButton* ui = xr_new<CUI3tButton>();
	if (parent)
	{
		parent->AttachChild(ui);
	}
	if (!CUIXmlInit::TryInit3tButton(xml, ui_path, 0, ui))
	{
		if (parent)
			parent->DetachChild(ui);
		xr_delete(ui);
		return nullptr;
	}
	if (parent)
		ui->SetAutoDelete(true);
	return ui;
}

CUIFrameLineWnd* UIHelper::CreateFrameLineOptional(CUIXml& xml, LPCSTR ui_path, CUIWindow* parent)
{
	CUIFrameLineWnd* ui = xr_new<CUIFrameLineWnd>();
	if (parent)
	{
		parent->AttachChild(ui);
	}
	if (!CUIXmlInit::TryInitFrameLine(xml, ui_path, 0, ui))
	{
		if (parent)
			parent->DetachChild(ui);
		xr_delete(ui);
		return nullptr;
	}
	if (parent)
		ui->SetAutoDelete(true);
	return ui;
}

CUIEditBox* UIHelper::CreateEditBoxOptional(CUIXml& xml, LPCSTR ui_path, CUIWindow* parent)
{
	CUIEditBox* ui = xr_new<CUIEditBox>();
	if (parent)
	{
		parent->AttachChild(ui);
	}
	if (!CUIXmlInit::TryInitEditBox(xml, ui_path, 0, ui))
	{
		if (parent)
			parent->DetachChild(ui);
		xr_delete(ui);
		return nullptr;
	}
	if (parent)
		ui->SetAutoDelete(true);
	return ui;
}
