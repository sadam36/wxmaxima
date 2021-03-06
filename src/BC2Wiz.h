///
///  Copyright (C) 2004-2011 Andrej Vodopivec <andrej.vodopivec@gmail.com>
///
///  This program is free software; you can redistribute it and/or modify
///  it under the terms of the GNU General Public License as published by
///  the Free Software Foundation; either version 2 of the License, or
///  (at your option) any later version.
///
///  This program is distributed in the hope that it will be useful,
///  but WITHOUT ANY WARRANTY; without even the implied warranty of
///  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
///  GNU General Public License for more details.
///
///
///  You should have received a copy of the GNU General Public License
///  along with this program; if not, write to the Free Software
///  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
///

#ifndef BC2WIZ_H
#define BC2WIZ_H

#include <wx/wx.h>
#include <wx/statline.h>

#include "BTextCtrl.h"

class BC2Wiz: public wxDialog
{
public:
  BC2Wiz(wxWindow* parent, int id, const wxString& title,
         const wxPoint& pos = wxDefaultPosition,
         const wxSize& size = wxDefaultSize,
         long style = wxDEFAULT_DIALOG_STYLE);
  void SetValue(wxString s)
  {
    text_ctrl_1->SetValue(s);
    text_ctrl_1->SetSelection(-1, -1);
  }
  wxString GetValue();
private:
  void set_properties();
  void do_layout();
  wxStaticText* label_2;
  BTextCtrl* text_ctrl_1;
  wxStaticText* label_3;
  BTextCtrl* text_ctrl_2;
  wxStaticText* label_4;
  BTextCtrl* text_ctrl_3;
  wxStaticText* label_5;
  BTextCtrl* text_ctrl_4;
  wxStaticText* label_6;
  BTextCtrl* text_ctrl_5;
  wxStaticLine* static_line_1;
  wxButton* button_1;
  wxButton* button_2;
};

#endif // BC2WIZ_H
