///
///  Copyright (C) 2004-2006 Andrej Vodopivec <andrejv@users.sourceforge.net>
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

#include "wxMaxima.h"
#include "Config.h"
#include "TextInput.h"
#include "SubstituteWiz.h"
#include "IntegrateWiz.h"
#include "LimitWiz.h"
#include "Plot2dWiz.h"
#include "SeriesWiz.h"
#include "SumWiz.h"
#include "Plot3dWiz.h"
#include "Gen1Wiz.h"
#include "Gen2Wiz.h"
#include "Gen3Wiz.h"
#include "Gen4Wiz.h"
#include "BC2Wiz.h"
#include "MatWiz.h"
#include "DragNDrop.h"
#include "SystemWiz.h"
#include "MathPrintout.h"
#include "MyTipProvider.h"
#include "EditorCell.h"

#include <wx/filedlg.h>
#include <wx/utils.h>
#include <wx/msgdlg.h>
#include <wx/textfile.h>
#include <wx/tokenzr.h>
#include <wx/mimetype.h>
#include <wx/dynlib.h>
#include <wx/dir.h>
#include <wx/filename.h>

enum {
  maxima_process_id
};

#if defined (__WXMAC__)
  #define MACPREFIX "wxMaxima.app/Contents/Resources/"
#endif

wxMaxima::wxMaxima(wxWindow *parent, int id, const wxString title,
                   const wxPoint pos, const wxSize size) :
    wxMaximaFrame(parent, id, title, pos, size)
{
  m_port = 4010;
  m_pid = -1;
  m_inLispMode = false;
  m_first = true;
  m_isRunning = false;
  m_promptSuffix = wxT("<PROMPT-S/>");
  m_promptPrefix = wxT("<PROMPT-P/>");
  m_commentPrefix = wxT("/*");
  m_newInput = wxT(">> ");
  GetMenuBar()->Enable(menu_interrupt_id, false);

  m_firstPrompt = wxT("(%i1) ");

  m_client = NULL;
  m_server = NULL;

  wxConfig::Get()->Read(wxT("lastPath"), &m_lastPath);
  m_lastPrompt = wxEmptyString;

#if wxUSE_DRAG_AND_DROP && WXM_DND
  m_console->SetDropTarget(new FileDrop(this));
#endif

#if WXM_PRINT
  CheckForPrintingSupport();
  m_printData = new wxPrintData;
  m_printData->SetQuality(wxPRINT_QUALITY_HIGH);
#endif

  m_inputLine->SetFocus();
  m_closing = false;
  m_openFile = wxEmptyString;
  m_inInsertMode = false;
  m_currentFile = wxEmptyString;
  m_fileSaved = true;

  m_variablesOK = false;

  m_batchFilePosition = 0;
  m_helpFile = wxEmptyString;
}

wxMaxima::~wxMaxima()
{
  if (m_client != NULL)
    m_client->Destroy();

#if WXM_PRINT
  delete m_printData;
#endif
}

///--------------------------------------------------------------------------------
///  Startup
///--------------------------------------------------------------------------------

#if WXM_PRINT

void wxMaxima::CheckForPrintingSupport()
{
#if defined __WXMSW__
  m_supportPrinting = true;
#elif defined __WXMAC__
  m_supportPrinting = true;

#elif defined wxUSE_LIBGNOMEPRINT
 #if wxUSE_LIBGNOMEPRINT
  wxLogNull log;
  wxDynamicLibrary* m_gnomep = new wxDynamicLibrary(wxT("libgnomeprint-2-2.so"));
  wxDynamicLibrary* m_gnomepui = new wxDynamicLibrary(wxT("libgnomeprintui-2-2.so"));
  if (m_gnomep->IsLoaded() && m_gnomepui->IsLoaded())
    m_supportPrinting = true;
  else
    m_supportPrinting = false;
  delete m_gnomep;
  delete m_gnomepui;
 #else
  m_supportPrinting = false;
 #endif
#else
  m_supportPrinting = false;
#endif
}

#endif

void wxMaxima::InitSession()
{
  bool server = false;
  int defaultPort = 4010;
  
  wxConfig::Get()->Read(wxT("defaultPort"), &defaultPort);
  m_port = defaultPort;

  while (!(server = StartServer()))
  {
    m_port++;
    if (m_port > 5000)
    {
      wxMessageBox(_("wxMaxima could not start the server.\n\n"
                     "Please check you have network support\n"
                     "enabled and try again!"),
                   _("Fatal error"),
                   wxOK | wxICON_ERROR);
    }
  }

  if (!server)
    SetStatusText(_("Starting server failed"));
  else if (!StartMaxima())
    SetStatusText(_("Starting maxima process failed"), 1);
}

void wxMaxima::FirstOutput(wxString s)
{
  wxConfigBase* config = wxConfig::Get();
  bool showHeader = true;
  config->Read(wxT("showHeader"), &showHeader);

  int start = s.Find(m_firstPrompt);
  m_console->ClearWindow();

  if (showHeader)
  {
    DoRawConsoleAppend(wxT("/*"), MC_TYPE_MAIN_PROMPT);
    ConsoleAppend(s.SubString(0, start - 1), MC_TYPE_TEXT);
  }

  HandleMainPrompt(m_firstPrompt);
}

/***
 * This reads the file as a wxMaxima batch file. If the file is OK we
 * setup inputs and comments and return true. If not we return false - wxMaxima
 * then uses batch to load the file!
 */
bool wxMaxima::ReadBatchFile(wxString file)
{
  wxTextFile inputFile(file);
  m_batchFileLines.Clear();

  if (!inputFile.Open())
  {
    wxMessageBox(_("Error opening file"), _("Error"));
    return false;
  }

  if (inputFile.GetFirstLine() !=
      wxT("/* [wxMaxima batch file version 1] [ DO NOT EDIT BY HAND! ]*/"))
    return false;

  wxString line;
  for (line = inputFile.GetFirstLine();
       !inputFile.Eof();
       line = inputFile.GetNextLine())
  {
    m_batchFileLines.Add(line);
  }
  m_batchFileLines.Add(line);

  inputFile.Close();
  m_batchFilePosition = 0;
  m_currentFile = file;

  return true;
}

///--------------------------------------------------------------------------------
///  Appending stuff to output
///--------------------------------------------------------------------------------

void wxMaxima::ConsoleAppend(wxString s, int type)
{
  m_inPrompt = false;
  m_dispReadOut = false;
  s.Replace(m_promptSuffix, wxEmptyString);

  wxString t(s);
  t.Trim();
  t.Trim(false);
  if (!t.Length())
    return ;

  if (type != MC_TYPE_ERROR)
    SetStatusText(_("Parsing output"), 1);
  if (type == MC_TYPE_TEXT)
  {
    while (s.Length() > 0)
    {
      int start = s.Find(wxT("<mth"));
      if (start == -1)
      {
        t = s;
        t.Trim();
        t.Trim(false);
        if (t.Length())
          DoRawConsoleAppend(s, MC_TYPE_TEXT);
        s = wxEmptyString;
      }
      else
      {
        wxString pre = s.SubString(0, start - 1);
        wxString pre1(pre);
        pre1.Trim();
        pre1.Trim(false);
        int end = s.Find(wxT("</mth>"));
        if (end == -1)
          end = s.Length();
        else
          end += 5;
        wxString rest = s.SubString(start, end);
        if (pre1.Length())
        {
          DoRawConsoleAppend(pre, MC_TYPE_TEXT);
          DoConsoleAppend(wxT("<span>") + rest +
                          wxT("</span>"), type, false);
        }
        else
        {
          DoConsoleAppend(wxT("<span>") + rest +
                          wxT("</span>"), type, false);
        }
        s = s.SubString(end + 1, s.Length());
      }
    }
  }
  else if (type == MC_TYPE_INPUT)
  {
    wxMessageBox(wxT("Should not be here!!!"));
  }
  else if (type == MC_TYPE_PROMPT)
  {
    SetStatusText(_("Ready for user input"), 1);
    m_lastPrompt = s;
    if (s.StartsWith(wxT("(%i")) || s == m_commentPrefix)
    {
      wxMessageBox(wxT("Should not be here!!! (1)"));
      m_inPrompt = true;
      type = MC_TYPE_MAIN_PROMPT;
    }
    else if (s.StartsWith(wxT("MAXIMA> ")))
    {
      m_inPrompt = true;
      s = s.Right(8);
    }
    else
      s = s + wxT(" ");
    DoConsoleAppend(wxT("<span>") + s + wxT("</span>"),
                    type, true);
  }
  else if (type == MC_TYPE_ERROR)
  {
    DoRawConsoleAppend(s, MC_TYPE_ERROR);
  }
  else
  {
    DoConsoleAppend(wxT("<span>") + s + wxT("</span>"),
                    type, false);
  }
}

void wxMaxima::DoConsoleAppend(wxString s, int type, bool newLine,
                               bool bigSkip)
{
  MathCell* cell;

  s.Replace(wxT("\n"), wxT(""), true);

  cell = m_MParser.ParseLine(s, type);

  if (cell == NULL)
  {
    wxMessageBox(_("There was an error in generated XML!\n\n"
                   "Please report this as a bug."), _("Error"),
                 wxOK | wxICON_EXCLAMATION);
    return ;
  }

  cell->SetSkip(bigSkip);
  m_console->InsertLine(cell, newLine || cell->BreakLineHere());
}

void wxMaxima::DoRawConsoleAppend(wxString s, int type, bool newLine)
{
  if (type == MC_TYPE_INPUT || type == MC_TYPE_COMMENT ||
      type == MC_TYPE_SECTION || type == MC_TYPE_TITLE)
  {
    ResetTitle(false);

    EditorCell* cell = new EditorCell();

    cell->SetValue(s);
    cell->SetType(type);

    m_console->InsertLine(cell, newLine);
  }
  else
  {
    wxStringTokenizer tokens(s, wxT("\n"));
    int count = 0;
    while (tokens.HasMoreTokens())
    {
      TextCell* cell = new TextCell(RemoveTabs(tokens.GetNextToken()));

      cell->SetType(type);

      if (tokens.HasMoreTokens())
        cell->SetSkip(false);

      if (count == 0)
        m_console->InsertLine(cell, newLine);
      else
        m_console->InsertLine(cell, true);
      count++;
    }
  }
}

void wxMaxima::SendMaxima(wxString s, bool clear, bool out, bool silent, bool split)
{
  if (!m_isConnected)
  {
    ConsoleAppend(wxT("\nNot connected to maxima!\n"), MC_TYPE_ERROR);
    return ;
  }

  if (!m_variablesOK)
  {
    m_variablesOK = true;
    SetupVariables();
  }

  if (s.StartsWith(wxT("<ml>")))
  {
    s = s.SubString(4, s.Length());
    s.Replace(wxT("<nl>"), wxT("\n"));
  }
  if (clear)
    m_inputLine->SetValue(wxEmptyString);
  if (out)
  {
    wxString s1(s);
    if (split)
      s1 = SplitInput(s);
    DoRawConsoleAppend(s1, MC_TYPE_INPUT, false);
  }
  if (silent)
  {
    m_inputLine->AddToHistory(s);
    SetStatusText(_("Maxima is calculating"), 1);
    m_dispReadOut = false;
  }
  s.Append(wxT("\n"));
  m_console->EnableEdit(false);

#if wxUSE_UNICODE
  char *buf;
  wxWX2MBbuf tmp = wxConvertWX2MB(s.wx_str());
  buf = strdup(tmp);
  m_client->Write(buf, strlen(buf));
  free(buf);
#else
  m_client->Write(s.c_str(), s.Length());
#endif
}

///--------------------------------------------------------------------------------
///  Modifying strings
///--------------------------------------------------------------------------------

wxString wxMaxima::SplitInput(wxString input)
{
  wxString seps(wxT(" +-"));
  wxString newInput;
  int col = 0;

  for (unsigned int i=0; i<input.Length(); i++)
  {
    if (col > 80 && seps.Find(input.GetChar(i)) > -1)
    {
      newInput += wxT("\n ");
      col = 0;
    }
    col++;
    newInput += input.GetChar(i);
    if (input.GetChar(i) == '\n')
      col = 0;
  }

  return newInput;
}

wxString wxMaxima::RemoveTabs(wxString s)
{
  int pos = 0;
  char c;
  wxString t = wxEmptyString;

  for (unsigned int i = 0; i < s.Length(); i++)
  {
    c = s.GetChar(i);
    if (c == '\t')
    {
      do
      {
        t += ' ';
        pos++;
      }
      while (pos % 8 != 0);
    }
    else
    {
      t += c;
      pos++;
    }
  }
  return t;
}

///--------------------------------------------------------------------------------
///  Socket stuff
///--------------------------------------------------------------------------------

void wxMaxima::ClientEvent(wxSocketEvent& event)
{
  char buffer[SOCKET_SIZE + 1];
  int read;
  switch (event.GetSocketEvent())
  {
  case wxSOCKET_INPUT:
    m_client->Read(buffer, SOCKET_SIZE);
    if (!m_client->Error())
    {
      read = m_client->LastCount();
      buffer[read] = 0;
      m_currentOutput += wxString(buffer, *wxConvCurrent);

      if (!m_dispReadOut && m_currentOutput != wxT("\n"))
      {
        SetStatusText(_("Reading maxima output"), 1);
        m_dispReadOut = true;
      }

      if (m_first && m_currentOutput.Find(m_firstPrompt) > -1)
        ReadFirstPrompt();

      ReadMath();

      ReadPrompt();

      ReadLispError();
    }
    break;
  case wxSOCKET_LOST:
    if (!m_closing)
      ConsoleAppend(wxT("\nCLIENT: Lost socket connection ...\n"
                        "Restart maxima with 'Maxima->Restart maxima'.\n"),
                    MC_TYPE_ERROR);
    m_pid = -1;
    GetMenuBar()->Enable(menu_interrupt_id, false);
    m_client->Destroy();
    m_client = NULL;
    m_isConnected = false;
    break;
  default:
    break;
  }
}

void wxMaxima::ServerEvent(wxSocketEvent& event)
{
  switch (event.GetSocketEvent())
  {
  case wxSOCKET_CONNECTION :
    {
      m_isConnected = true;
      m_client = m_server->Accept(false);
      m_client->SetEventHandler(*this, socket_client_id);
      m_client->SetNotify(wxSOCKET_INPUT_FLAG | wxSOCKET_LOST_FLAG);
      m_client->Notify(true);
#ifndef __WXMSW__
      ReadProcessOutput();
#endif
    }
    break;
  case wxSOCKET_LOST:
    if (!m_closing)
      ConsoleAppend(wxT("\nSERVER: Lost socket connection ...\n"
                        "Restart maxima with 'Maxima->Restart maxima'.\n"),
                    MC_TYPE_ERROR);
    m_pid = -1;
    GetMenuBar()->Enable(menu_interrupt_id, false);
    m_isConnected = false;
  default:
    break;
  }
}

bool wxMaxima::StartServer()
{
  SetStatusText(wxString::Format(_("Starting server on port %d"), m_port), 1);

  wxIPV4address addr;

  addr.AnyAddress();
  addr.Service(m_port);

  m_server = new wxSocketServer(addr);
  if (!m_server->Ok())
  {
    delete m_server;
    m_isRunning = false;
    SetStatusText(_("Starting server failed"), 1);
    return false;
  }
  SetStatusText(_("Server started"), 1);
  m_server->SetEventHandler(*this, socket_server_id);
  m_server->SetNotify(wxSOCKET_CONNECTION_FLAG);
  m_server->Notify(true);

  m_isConnected = false;
  m_isRunning = true;
  return m_isRunning;
}

///--------------------------------------------------------------------------------
///  Maxima process stuff
///--------------------------------------------------------------------------------

bool wxMaxima::StartMaxima()
{
  if (m_isConnected)
  {
    KillMaxima();
    //    m_client->Close();
    m_isConnected = false;
  }

  m_variablesOK = false;
  wxString command = GetCommand();

  if (command.Length() > 0)
  {
#if defined(__WXMSW__)
    if (wxGetOsVersion() == wxWIN95)
    {
      wxString maximaPrefix = command.SubString(1, command.Length() - 3);
      wxString sysPath;

      wxGetEnv(wxT("path"), &sysPath);
      maximaPrefix.Replace(wxT("\\bin\\maxima.bat"), wxEmptyString);

      wxSetEnv(wxT("maxima_prefix"), maximaPrefix);
      wxSetEnv(wxT("path"), maximaPrefix + wxT("\\bin;") + sysPath);

      command = maximaPrefix + wxT("\\lib\\maxima");
      if (!wxDirExists(command))
        return false;

      wxArrayString files;
      wxDir::GetAllFiles(command, &files, wxT("maxima.exe"));
      if (files.Count() == 0)
        return false;
      else
      {
        command = files[0];
        command.Append(wxString::Format(
                         wxT(" -eval \"(maxima::start-server %d)\" -eval \"(run)\" -f"),
                         m_port
                       ));
      }
    }
    else
      command.Append(wxString::Format(wxT(" -s %d"), m_port));
    wxSetEnv(wxT("home"), wxGetHomeDir());
#else
    command.Append(wxString::Format(wxT(" -r \":lisp (setup-server %d)\""),
                                    m_port));
#endif

    m_process = new wxProcess(this, maxima_process_id);
    m_process->Redirect();
    m_first = true;
    GetMenuBar()->Enable(menu_interrupt_id, false);
    m_pid = -1;
    SetStatusText(_("Starting maxima..."), 1);
    wxExecute(command, wxEXEC_ASYNC, m_process);
    m_input = m_process->GetInputStream();
    SetStatusText(_("Maxima started. Waiting for connection..."), 1);
  }
  else
    return false;
  return true;
}


void wxMaxima::Interrupt(wxCommandEvent& event)
{
  if (m_pid < 0)
  {
    GetMenuBar()->Enable(menu_interrupt_id, false);
    return ;
  }
#if defined (__WXMSW__)
  wxString path, maxima;
  wxArrayString out;
  wxConfig::Get()->Read(wxT("maxima"), &maxima);
  wxFileName::SplitPath(maxima, &path, NULL, NULL);
  wxString command = wxT("\"") + path + wxT("\\winkill.exe\"");
  command += wxString::Format(wxT(" -INT %ld"), m_pid);
  wxExecute(command, out);
#else
  wxProcess::Kill(m_pid, wxSIGINT);
#endif
}

void wxMaxima::KillMaxima()
{
  m_process->Detach();
  if (m_pid < 0)
  {
    if (m_inLispMode)
      SendMaxima(wxT("($quit)"), false, false);
    else
      SendMaxima(wxT("quit();"), false, false);
    return ;
  }
  wxProcess::Kill(m_pid, wxSIGKILL);
}

void wxMaxima::OnProcessEvent(wxProcessEvent& event)
{
  if (!m_closing)
    SetStatusText(_("Maxima process terminated."), 1);
//  delete m_process;
//  m_process = NULL;
}

void wxMaxima::CleanUp()
{
  if (m_isConnected)
  {
    KillMaxima();
  }
  if (m_isRunning)
  {
    m_server->Destroy();
  }
}

///--------------------------------------------------------------------------------
///  Dealing with stuff read from the socket
///--------------------------------------------------------------------------------

void wxMaxima::ReadFirstPrompt()
{
#if defined(__WXMSW__)
  int start = m_currentOutput.Find(wxT("Maxima"));
  if (start == -1)
    start = 0;
  FirstOutput(wxT("wxMaxima ")
              wxT(VERSION)
              wxT(" http://wxmaxima.sourceforge.net\n") +
              m_currentOutput.SubString(start, m_currentOutput.Length() - 1));
#endif // __WXMSW__

  int s = m_currentOutput.Find(wxT("pid=")) + 4;
  int t = s + m_currentOutput.SubString(s, m_currentOutput.Length()).Find(wxT("\n"));
  if (s < t)
    m_currentOutput.SubString(s, t).ToLong(&m_pid);
  if (m_pid > 0)
    GetMenuBar()->Enable(menu_interrupt_id, true);
  m_first = false;
  m_inLispMode = false;
  SetStatusText(_("Ready for user input"), 1);
  if (m_openFile.Length())
  {
    OpenFile(m_openFile);
    m_openFile = wxEmptyString;
  }
  m_currentOutput = wxEmptyString;
  m_console->EnableEdit(true);
}

void wxMaxima::ReadMath()
{
  int end = m_currentOutput.Find(m_promptPrefix);
  while (end > -1)
  {
    m_readingPrompt = true;
    wxString o = m_currentOutput.Left(end);
    ConsoleAppend(o, MC_TYPE_TEXT);
    m_currentOutput = m_currentOutput.SubString(end + m_promptPrefix.Length(),
                      m_currentOutput.Length());
    end = m_currentOutput.Find(m_promptPrefix);
  }
  if (m_readingPrompt)
    return ;
  wxString mth = wxT("</mth>");
  end = m_currentOutput.Find(mth);
  while (end > -1)
  {
    wxString o = m_currentOutput.Left(end);
    ConsoleAppend(o + mth, MC_TYPE_TEXT);
    m_currentOutput = m_currentOutput.SubString(end + mth.Length(),
                      m_currentOutput.Length());
    end = m_currentOutput.Find(mth);
  }
}

void wxMaxima::ReadPrompt()
{
  int end = m_currentOutput.Find(m_promptSuffix);
  if (end > -1)
  {
    m_readingPrompt = false;
    wxString o = m_currentOutput.Left(end);
    if (o != wxT("\n") && o.Length())
    {
      if (o.StartsWith(wxT("(%i")))
      {
        if (m_inInsertMode && o.StartsWith(wxT("(%i")))
        {
          MathCell* tmp = m_console->GetInsertPoint();
          m_console->SetInsertPoint(NULL);
          m_console->SetScrollTo(-1);
          m_inInsertMode = false;
          m_lastPrompt = o;
          wxYield();  // Let's redraw the output windows so that we get new positions!
          m_console->SetSelection(tmp);
          if (!m_console->SelectNextInput())
            m_console->SetSelection(NULL);
          m_console->GetLastPrompt()->SetValue(o);
          m_console->Refresh();
          SetStatusText(_("Ready for user input"), 1);
        }
        else
          HandleMainPrompt(o);
        m_console->EnableEdit();
      }
      else
      {
        if (o.Find(wxT("<mth>")) > -1)
          DoConsoleAppend(o, MC_TYPE_PROMPT);
        else
          DoRawConsoleAppend(o, MC_TYPE_PROMPT);
      }
      if (o.StartsWith(wxT("\nMAXIMA>")))
        m_inLispMode = true;
      else
        m_inLispMode = false;
    }
    SetStatusText(_("Ready for user input"), 1);
    m_currentOutput = m_currentOutput.SubString(end + m_promptSuffix.Length(),
                      m_currentOutput.Length());
  }
}

/***
 * This is used when we are batching wxMaxima files
 * - if we still have something to send to maxima we send it (and before that append comments!)
 * - mostly we just append the prompt!
 */
void wxMaxima::HandleMainPrompt(wxString o)
{
  m_lastPrompt = o;
  if (m_batchFileLines.IsEmpty()) {
    DoRawConsoleAppend(o, MC_TYPE_MAIN_PROMPT);
  }
  else
  {
    bool done = false;
    wxString line;
    while (!done && m_batchFilePosition < m_batchFileLines.GetCount())
    {
      line = m_batchFileLines[m_batchFilePosition++];
      if (line == wxT("/* [wxMaxima: comment start ]"))
      {
        DoRawConsoleAppend(wxT("/*"), MC_TYPE_MAIN_PROMPT);
        line = m_batchFileLines[m_batchFilePosition++];
        bool newline = false;
        wxString input;
        while (line != wxT("   [wxMaxima: comment end   ] */") &&
               m_batchFilePosition < m_batchFileLines.GetCount()) {
          if (newline)
            input = input + wxT("\n") + line;
          else
          {
            input = line;
            newline = true;
          }
          line = m_batchFileLines[m_batchFilePosition++];
        }
        DoRawConsoleAppend(input, MC_TYPE_COMMENT, true);
      }
      else if (line == wxT("/* [wxMaxima: section start ]"))
      {
        DoRawConsoleAppend(wxT("/*"), MC_TYPE_MAIN_PROMPT);
        line = m_batchFileLines[m_batchFilePosition++];
        bool newline = false;
        wxString input;
        while (line != wxT("   [wxMaxima: section end   ] */") &&
               m_batchFilePosition < m_batchFileLines.GetCount()) {
          if (newline)
            input = input + wxT("\n") + line;
          else
          {
            input = line;
            newline = true;
          }
          line = m_batchFileLines[m_batchFilePosition++];
        }
        DoRawConsoleAppend(input, MC_TYPE_SECTION, true);
      }
      else if (line == wxT("/* [wxMaxima: title   start ]"))
      {
        DoRawConsoleAppend(wxT("/*"), MC_TYPE_MAIN_PROMPT);
        line = m_batchFileLines[m_batchFilePosition++];
        bool newline = false;
        wxString input;
        while (line != wxT("   [wxMaxima: title   end   ] */") &&
               m_batchFilePosition < m_batchFileLines.GetCount()) {
          if (newline)
            input = input + wxT("\n") + line;
          else
          {
            input = line;
            newline = true;
          }
          line = m_batchFileLines[m_batchFilePosition++];
        }
        DoRawConsoleAppend(input, MC_TYPE_TITLE, true);
      }
      else if (line == wxT("/* [wxMaxima: input   start ] */"))
      {
        line = m_batchFileLines[m_batchFilePosition++];
        DoRawConsoleAppend(o, MC_TYPE_MAIN_PROMPT);
        bool newline = false;
        wxString input;
        while (line != wxT("/* [wxMaxima: input   end   ] */") &&
               m_batchFilePosition < m_batchFileLines.GetCount())
        {
          if (newline)
            input = input + wxT("\n") + line;
          else
          {
            input = line;
            newline = true;
          }
          line = m_batchFileLines[m_batchFilePosition++];
        }
        SendMaxima(input, true, true, true, false);
        done = true;
      }
    }
    if (!done)
      DoRawConsoleAppend(o, MC_TYPE_MAIN_PROMPT);

    if (m_batchFileLines.GetCount() <= m_batchFilePosition)
    {
      ResetTitle(true);
      m_batchFilePosition = 0;
      m_batchFileLines.Clear();
    }
  }
}

/***
 * This works only for gcl by default - other lisps have different prompts.
 */
void wxMaxima::ReadLispError()
{
  static const wxString lispError = wxT("dbl:MAXIMA>>"); // gcl
  int end = m_currentOutput.Find(lispError);
  if (end > -1)
  {
    m_readingPrompt = false;
    m_inLispMode = true;
    wxString o = m_currentOutput.Left(end);
    ConsoleAppend(o, MC_TYPE_TEXT);
    ConsoleAppend(lispError, MC_TYPE_PROMPT);
    SetStatusText(_("Ready for user input"), 1);
    m_currentOutput = wxEmptyString;
  }
}

#ifndef __WXMSW__
void wxMaxima::ReadProcessOutput()
{
  wxString o;
  while (m_process->IsInputAvailable())
  {
    o += m_input->GetC();
  }
  int st = o.Find(wxT("Maxima"));
  if (st == -1)
    st = 0;
  FirstOutput(wxT("wxMaxima ")
              wxT(VERSION)
              wxT(" http://wxmaxima.sourceforge.net\n") +
              o.SubString(st, o.Length() - 1));
  m_inPrompt = true;
  SetStatusText(_("Ready for user input"), 1);
}
#endif

void wxMaxima::SetupVariables()
{
  SendMaxima(wxT(":lisp-quiet (setf *prompt-suffix* \"") +
             m_promptSuffix +
             wxT("\")"), false, false, false);
  SendMaxima(wxT(":lisp-quiet (setf *prompt-prefix* \"") +
             m_promptPrefix +
             wxT("\")"), false, false, false);
  SendMaxima(wxT(":lisp-quiet (setf $IN_NETMATH nil)"), false, false, false);
  SendMaxima(wxT(":lisp-quiet (setf $SHOW_OPENPLOT t)"), false, false, false);
#if defined (__WXMSW__)
  wxString cwd = wxGetCwd();
  cwd.Replace(wxT("\\"), wxT("/"));
  SendMaxima(wxT(":lisp-quiet ($load \"") + cwd + wxT("/data/wxmathml\")"),
             false, false, false);
#elif defined (__WXMAC__)
  wxString cwd = wxGetCwd();
  cwd = cwd + wxT("/") + wxT(MACPREFIX);
  SendMaxima(wxT(":lisp-quiet ($load \"") + cwd + wxT("wxmathml\")"),
             false, false, false);
#else
  wxString prefix = wxT(PREFIX);
  SendMaxima(wxT(":lisp-quiet ($load \"") + prefix +
             wxT("/share/wxMaxima/wxmathml\")"), false, false, false);
#endif
}

///--------------------------------------------------------------------------------
///  Getting confuguration
///--------------------------------------------------------------------------------

wxString wxMaxima::GetCommand()
{
#if defined (__WXMSW__)
  wxConfig *config = (wxConfig *)wxConfig::Get();
  wxString maxima = wxGetCwd();
  wxString command, parameters;

  maxima.Replace(wxT("wxMaxima"), wxT("bin\\maxima.bat"));

  if (!wxFileExists(maxima))
  {
    config->Read(wxT("maxima"), &maxima);
    if (!wxFileExists(maxima))
    {
      wxMessageBox(_("wxMaxima could not find maxima!\n\n"
                     "Please configure wxMaxima with 'Edit->Configure'.\n"
                     "Then start maxima with 'Maxima->Restart maxima'."), _("Warning"),
                   wxOK | wxICON_EXCLAMATION);
      SetStatusText(_("Please configure wxMaxima with 'Edit->Configure'."));
      return wxEmptyString;
    }
  }

  config->Read(wxT("parameters"), &parameters);
  command = wxT("\"") + maxima + wxT("\" ") + parameters;
  return command;
#else
  wxConfig *config = (wxConfig *)wxConfig::Get();
  wxString command, parameters;
  bool have_config = config->Read(wxT("maxima"), &command);

  if (!have_config)
  {
    command = wxT("maxima");
    config->Write(wxT("maxima"), command);
  }

  config->Read(wxT("parameters"), &parameters);
  command = wxT("\"") + command + wxT("\" ") + parameters;
  return command;
#endif
}

///--------------------------------------------------------------------------------
///  Tips and help
///--------------------------------------------------------------------------------

void wxMaxima::ShowTip(bool force)
{
  bool ShowTips = true;
  int tipNum = 0;
  wxConfig *config = (wxConfig *)wxConfig::Get();
  config->Read(wxT("ShowTips"), &ShowTips);
  config->Read(wxT("tipNum"), &tipNum);
  if (!ShowTips && !force)
    return ;
  wxString tips = wxT("tips.txt");
#if defined (__WXMSW__)
  wxString prefix = wxGetCwd() + wxT("\\data\\");
#elif defined (__WXMAC__)
  wxString prefix = wxT(MACPREFIX);
  prefix += wxT("/");
#else
  wxString prefix = wxT(PREFIX);
  prefix += wxT("/share/wxMaxima/");
#endif

  tips = prefix + tips;
  if (wxFileExists(tips))
  {
    MyTipProvider *t = new MyTipProvider(tips, tipNum);
    ShowTips = wxShowTip(this, t, ShowTips);
    config->Write(wxT("ShowTips"), ShowTips);
    tipNum = t->GetCurrentTip();
    config->Write(wxT("tipNum"), tipNum);
    config->Flush();
    delete t;
  }
  else
  {
    wxMessageBox(_("wxMaxima could not find tip files."
                   "\n\nPlease check your installation."),
                 _("Error"), wxICON_ERROR | wxOK);
  }
}

wxString wxMaxima::GetHelpFile()
{
#if defined __WXMSW__
  wxString command;
  wxString html;

  command = wxGetCwd();
  command.Replace(wxT("wxMaxima"), wxT("bin\\maxima.bat"));

  if (!wxFileExists(command))
    wxConfig::Get()->Read(wxT("maxima"), &command);

  if (command.empty())
    return wxEmptyString;

  command.Replace(wxT("bin\\maxima.bat"), wxT("share\\maxima"));

  html = wxFindFirstFile(command + wxT("\\*"), wxDIR);

  if (html.empty())
    return wxEmptyString;

  html = html + wxT("\\doc\\html\\");

  wxString locale = wxGetApp().m_locale.GetCanonicalName().Left(2);

  if (wxFileExists(html + locale + wxT("\\header.hhp")))
    return html + locale + wxT("\\header.hhp");

  if (wxFileExists(html + wxT("header.hhp")))
    return html + wxT("header.hhp");

  return wxEmptyString;
#else
  wxProcess *process = new wxProcess(this, -1);
  process->Redirect();
  wxString command = GetCommand();
  command += wxT(" -d");
  wxArrayString output;
  wxExecute(command, output, wxEXEC_ASYNC);

  wxString line;
  wxString docdir;
  wxString langsubdir;

  for (unsigned int i=0; i<output.GetCount(); i++)
  {
    line = output[i];
    if (line.StartsWith(wxT("maxima-htmldir")))
      docdir = line.Mid(15);
    else if (line.StartsWith(wxT("maxima-lang-subdir")))
    {
      langsubdir = line.Mid(19);
      if (langsubdir == wxT("NIL"))
        langsubdir = wxEmptyString;
    }
  }

  if (docdir.Length() == 0)
    return wxEmptyString;

  wxString headerFile = docdir + wxT("/");
  if (langsubdir.Length())
    headerFile += langsubdir + wxT("/");

  headerFile += wxT("header.hhp");

  if (!wxFileExists(headerFile))
    headerFile = docdir + wxT("/header.hhp");

  return headerFile;
#endif
}

void wxMaxima::ShowHelp(wxString keyword)
{
  if (m_helpFile.Length() == 0)
  {
    m_helpFile = GetHelpFile();
    if (m_helpFile.Length() == 0)
    {
      wxMessageBox(_("wxMaxima could not find help files."
                     "\n\nPlease check your installation."),
                   _("Error"), wxICON_ERROR | wxOK);
      return ;
    }

    m_helpCtrl.Initialize(m_helpFile);
  }

  if (keyword == wxT("%"))
    m_helpCtrl.DisplayContents();
  else
    m_helpCtrl.KeywordSearch(keyword, wxHELP_SEARCH_INDEX);
}

///--------------------------------------------------------------------------------
///  Menu and button events
///--------------------------------------------------------------------------------

void wxMaxima::DumpProcessOutput()
{
  wxString o;
  while (m_process->IsInputAvailable())
  {
    o += m_input->GetC();
  }
  
  wxMessageBox(o, wxT("Process output (stdout)"));
  
  o = wxEmptyString;
  wxInputStream *error = m_process->GetErrorStream();
  while (m_process->IsErrorAvailable())
  {
    o += error->GetC();
  }
  
  wxMessageBox(o, wxT("Process output (stderr)"));
  
}

///--------------------------------------------------------------------------------
///  Menu and button events
///--------------------------------------------------------------------------------

void wxMaxima::EnterCommand(wxCommandEvent& event)
{
  wxString input = m_inputLine->GetValue();
  input.Trim();
  input.Trim(false);
  if (input.StartsWith(wxT("? ")))
  {
    ShowHelp(input.Mid(2));
    m_inputLine->Clear();
    return ;
  }
  if (input == wxT("?"))
  {
    ShowHelp();
    m_inputLine->Clear();
    return ;
  }
  if (input == wxT("wxmaxima_debug_dump_output"))
  {
    DumpProcessOutput();
    m_inputLine->Clear();
    return ;
  }
  if (!m_inLispMode &&
          (input.Length() == 0 || (input.Last() != ';' && input.Last() != '$')))
    input.Append(';');
  SendMaxima(input);
  m_inputLine->Clear();
  m_inputLine->SetFocus();
}

#if WXM_PRINT

void wxMaxima::PrintMenu(wxCommandEvent& event)
{
  switch (event.GetId())
  {
  case wxID_PRINT:
#if defined (__WXMSW__) || defined (__WXGTK20__)
  case tb_print:
#endif
    {
      wxPrintDialogData printDialogData(*m_printData);
      wxPrinter printer(&printDialogData);
      wxString title(_("Maxima session"));
      if (m_currentFile.Length())
        wxFileName::SplitPath(m_currentFile, NULL, NULL, &title, NULL);
      MathPrintout printout(title);
      MathCell* copy = m_console->CopyTree();
      printout.SetData(copy);
      /*
            if (!printer.Print(this, &printout, true)) {
              if (wxPrinter::GetLastError() == wxPRINTER_ERROR)
                wxMessageBox(_("There was a problem printing.\n"
                               "Perhaps your current printer is not set correctly?"),
                             _("Printing"), wxOK);
            }
            else {
              (*m_printData) = printer.GetPrintDialogData().GetPrintData();
            }
      */
      if (printer.Print(this, &printout, true))
        (*m_printData) = printer.GetPrintDialogData().GetPrintData();
      break;
    }
  }
}

#endif

void wxMaxima::UpdateMenus(wxUpdateUIEvent& event)
{
  wxMenuBar* menubar = GetMenuBar();
  menubar->Enable(menu_copy_from_console, m_console->CanCopy(true) || m_inputLine->CanCopy());
  menubar->Enable(menu_cut, m_console->CanCut() || m_inputLine->CanCut());
  menubar->Enable(menu_paste, m_inputLine->CanPaste());
  menubar->Enable(menu_copy_lb_from_console, m_console->CanCopy());
  menubar->Enable(menu_copy_tex_from_console, m_console->CanCopy());
  menubar->Enable(menu_selection_to_input, m_console->CanCopy());
  menubar->Enable(menu_copy_as_bitmap, m_console->CanCopy());
  menubar->Enable(menu_copy_to_file, m_console->CanCopy());
  menubar->Enable(menu_delete_selection, m_console->CanDeleteSelection());
  menubar->Enable(menu_edit_input, m_console->CanEdit());
  menubar->Enable(menu_reeval_input, m_console->CanEdit());
  menubar->Enable(menu_add_comment, m_console->CanAddComment());
  menubar->Enable(menu_add_title, m_console->CanAddComment());
  menubar->Enable(menu_add_section, m_console->CanAddComment());
  menubar->Enable(menu_insert_input, m_console->CanAddInput());
  menubar->Enable(menu_save_id, !m_fileSaved);
#if WXM_PRINT
  if (m_console->GetTree() != NULL && m_supportPrinting)
    menubar->Enable(wxID_PRINT, true);
  else
    menubar->Enable(wxID_PRINT, false);
#endif
  int fontSize = 12;
  wxConfig::Get()->Read(wxT("fontSize"), &fontSize);
  if (fontSize < 20)
    menubar->Enable(menu_inc_fontsize, true);
  else
    menubar->Enable(menu_inc_fontsize, false);
  if (fontSize > 8)
    menubar->Enable(menu_dec_fontsize, true);
  else
    menubar->Enable(menu_dec_fontsize, false);
}

#if defined (__WXMSW__) || defined (__WXGTK20__)

void wxMaxima::UpdateToolBar(wxUpdateUIEvent& event)
{
  wxToolBar * toolbar = GetToolBar();
  toolbar->EnableTool(tb_copy,  m_console->CanCopy(true) || m_inputLine->CanCopy());
  toolbar->EnableTool(tb_delete, m_console->CanDeleteSelection());
  toolbar->EnableTool(tb_insert_text, m_console->CanAddComment());
  toolbar->EnableTool(tb_insert_input, m_console->CanAddInput());
  toolbar->EnableTool(tb_save, !m_fileSaved);
  if (m_pid > 0)
    toolbar->EnableTool(tb_interrupt, true);
  else
    toolbar->EnableTool(tb_interrupt, false);
#if WXM_PRINT
  if (m_console->GetTree() != NULL && m_supportPrinting)
    toolbar->EnableTool(tb_print, true);
  else
    toolbar->EnableTool(tb_print, false);
#endif
}

#endif

wxString wxMaxima::GetDefaultEntry()
{
  wxString s = m_inputLine->GetValue();
  if (s.Length() == 0)
  {
    if (m_console->CanCopy())
      s = m_console->GetString();
    else
      s = wxT("%");
  }
  return s;
}

void wxMaxima::OnMonitorFile(wxCommandEvent& event)
{
  wxString file = wxFileSelector(_("Select package to load"), m_lastPath,
                                 wxEmptyString, wxEmptyString,
                                 _("Maxima package (*.mac)|*.mac|"
                                   "Lisp package (*.lisp)|*.lisp|All|*"),
                                 wxOPEN);
  if (file.Length() != 0)
  {
    m_monitorFile = file;
#if defined __WXMSW__
    m_monitorFile.Replace(wxT("\\"), wxT("/"));
#endif

    m_monitorTime = wxFileModificationTime(file);
    SetStatusText(wxT("Monitoring file ") + m_monitorFile);
  }
}

void wxMaxima::OnActivate(wxActivateEvent& event)
{
  if (m_monitorFile.Length() != 0)
  {
    if (m_monitorTime != wxFileModificationTime(m_monitorFile))
    {
      if (m_inPrompt)
      {
        SendMaxima(wxT("load(\"") + m_monitorFile + wxT("\");"));
        SetStatusText(wxT("Reloaded file ") + m_monitorFile);
        m_monitorTime = wxFileModificationTime(m_monitorFile);
      }
    }
  }
  event.Skip();
}

void wxMaxima::OnSetFocus(wxFocusEvent& event)
{
  if (m_monitorFile.Length() != 0)
  {
    if (m_monitorTime != wxFileModificationTime(m_monitorFile))
    {
      if (m_inPrompt)
      {
        SendMaxima(wxT("load(\"") + m_monitorFile + wxT("\");"));
        SetStatusText(wxT("Reloaded file ") + m_monitorFile);
        m_monitorTime = wxFileModificationTime(m_monitorFile);
      }
    }
  }
  event.Skip();
}

void wxMaxima::OpenFile(wxString file, wxString cmd)
{
  if (file.Length())
  {
    m_lastPath = wxPathOnly(file);
#if defined __WXMSW__
    file.Replace(wxT("\\"), wxT("/"));
#endif

    if (cmd.Length() > 0)
    {
      SendMaxima(cmd + wxT("(\"") + file + wxT("\")$"));
    }
    else if (file.Right(4) == wxT(".wxm"))
    {
      if (!ReadBatchFile(file))
        SendMaxima(wxT("batch(\"") + file + wxT("\")$"));
      else
        StartMaxima();
    }
    else if (file.Right(4) == wxT(".sav"))
    {
      m_console->DestroyTree();
      SendMaxima(wxT("loadsession(\"") + file + wxT("\")$"), false, false, false);
    }
    else if (file.Right(4) == wxT(".dem"))
      SendMaxima(wxT("demo(\"") + file + wxT("\")$"));
    else
      SendMaxima(wxT("load(\"") + file + wxT("\")$"));
  }
}

void wxMaxima::FileMenu(wxCommandEvent& event)
{
  wxString expr = GetDefaultEntry();
  wxString cmd;
#if defined __WXMSW__
  wxString b = wxT("\\");
  wxString f = wxT("/");
#endif

  switch (event.GetId())
  {
#if defined (__WXMSW__) || defined (__WXGTK20__)
  case tb_open:
#endif
  case menu_open_id:
    {
      wxString file = wxFileSelector(_("Select file to open"), m_lastPath,
                                     wxEmptyString, wxEmptyString,
                                     _("wxMaxima session (*.wxm)|*.wxm"),
                                     wxOPEN);
      OpenFile(file);
    }
    break;
  case menu_save_as_id:
    {
      wxString file(_("untitled"));
      if (m_currentFile.Length() >0)
        wxFileName::SplitPath(m_currentFile, NULL, NULL, &file, NULL);
      file = wxFileSelector(_("Save to file"), m_lastPath,
                            file + wxT(".wxm"), wxT("wxm"),
                            _("wxMaxima session (*.wxm)|*.wxm"),
                            wxSAVE | wxOVERWRITE_PROMPT);
      if (file.Length())
      {
        m_currentFile = file;
        m_lastPath = wxPathOnly(file);
        if (wxFileExists(file))
          wxRemoveFile(file);
        if (file.Right(4) != wxT(".wxm"))
          file = file + wxT(".wxm");
        m_console->ExportToMAC(file);
        m_fileSaved = false;
        ResetTitle(true);
      }
    }
    break;
#if defined (__WXMSW__) || defined (__WXGTK20__)
  case tb_save:
#endif
  case menu_save_id:
    {
      wxString file = m_currentFile;
      if (m_currentFile.Length() == 0)
      {
        file = wxFileSelector(_("Save to file"), m_lastPath,
                              _("untitled.wxm"), wxT("wxm"),
                              _("Maxima session (*.wxm)|*.wxm"),
                              wxSAVE | wxOVERWRITE_PROMPT);
      }
      if (file.Length())
      {
        m_currentFile = file;
        m_lastPath = wxPathOnly(file);
        if (file.Right(4) != wxT(".wxm"))
          file = file + wxT(".wxm");
        if (wxFileExists(file))
          wxRemoveFile(file);
        m_console->ExportToMAC(file);
        ResetTitle(true);
      }
    }
    break;
  case menu_export_html:
    {
      wxString file(_("untitled"));
      if (m_currentFile.Length() >0)
        wxFileName::SplitPath(m_currentFile, NULL, NULL, &file, NULL);
      file = wxFileSelector(_("Export to HTML file"), m_lastPath,
                            file + wxT(".html"), wxT("html"),
                            _("HTML file (*.html)|*.html|"
                              "All|*"),
                            wxSAVE | wxOVERWRITE_PROMPT);
      if (file.Length())
      {
        m_lastPath = wxPathOnly(file);
        if (file.Right(5) != wxT(".html"))
          file = file + wxT(".html");
        if (!m_console->ExportToHTML(file))
          wxMessageBox(_("Exporting to HTML failed!"), _("Error!"),
                       wxOK);
      }
    }
    break;
  case menu_load_id:
    {
      wxString file = wxFileSelector(_("Select package to load"), m_lastPath,
                                     wxEmptyString, wxEmptyString,
                                     _("Maxima package (*.mac)|*.mac|"
                                       "Lisp package (*.lisp)|*.lisp|All|*"),
                                     wxOPEN);
      OpenFile(file, wxT("load"));
    }
    break;
  case menu_batch_id:
    {
      wxString file = wxFileSelector(_("Select package to load"), m_lastPath,
                                     wxEmptyString, wxEmptyString,
                                     _("Maxima package (*.mac)|*.mac|"),
                                     wxOPEN);
      OpenFile(file, wxT("batch"));
    }
    break;
  case menu_select_file:
    {
      wxString file = wxFileSelector(_("Select a file"), m_lastPath,
                                     wxEmptyString, wxEmptyString,
                                     _("All|*|"
                                       "Maxima package (*.mac)|*.mac|"
                                       "Demo file (*.dem)|*.dem|"
                                       "Lisp file (*.lisp)|*.lisp"),
                                     wxOPEN);
      if (file.Length())
      {
        m_lastPath = wxPathOnly(file);
#if defined __WXMSW__
        file.Replace(b, f);
#endif
        m_inputLine->WriteText(file);
        return ;
      }
    }
    break;
  case wxID_EXIT:
    Close();
    break;
  default:
    break;
  }
}

void wxMaxima::EditMenu(wxCommandEvent& event)
{
  switch (event.GetId())
  {
  case menu_goto_output:
    m_console->SetFocus();
    return;
    break;
  case wxID_PREFERENCES:
#if defined (__WXMSW__) || defined (__WXGTK20__)
  case tb_pref:
#endif
    {
      Config *configW = new Config(this, -1, _("wxMaxima configuration"));
      configW->Centre(wxBOTH);
      if (configW->ShowModal() == wxID_OK)
      {
        bool match = true;
        wxConfig::Get()->Read(wxT("matchParens"), &match);
        m_inputLine->SetMatchParens(match);
        m_console->RecalculateForce();
        m_console->Refresh();
      }
      configW->Destroy();
    }
    break;
  case menu_clear_screen:
    m_console->ClearWindow();
    DoRawConsoleAppend(m_lastPrompt, MC_TYPE_MAIN_PROMPT);
    break;
#if defined (__WXMSW__) || defined (__WXGTK20__)
  case tb_copy:
#endif
  case menu_copy_from_console:
    if (m_console->CanCopy())
      m_console->Copy();
    else if (m_inputLine->CanCopy())
      m_inputLine->Copy();
    break;
  case menu_cut:
    if (m_console->CanCut())
      m_console->CutToClipboard();
    else if (m_inputLine->CanCut())
      m_inputLine->Cut();
    break;
  case menu_paste:
    if (m_console->CanPaste())
      m_console->PasteFromClipboard();
    else if (m_inputLine->CanPaste())
      m_inputLine->Paste();
    break;
  case menu_copy_lb_from_console:
    if (m_console->CanCopy())
      m_console->Copy(true);
    break;
  case menu_copy_tex_from_console:
    if (m_console->CanCopy())
      m_console->CopyTeX();
    break;
  case menu_copy_as_bitmap:
    if (m_console->CanCopy())
      m_console->CopyBitmap();
    break;
  case menu_selection_to_input:
    if (m_console->CanCopy())
      m_inputLine->WriteText((m_console->GetString()).Trim(false));
    return ;
    break;
  case menu_copy_to_file:
    {
      wxString file = wxFileSelector(_("Save selection to file"), m_lastPath,
                                     wxT("image.png"), wxT("png"),
                                     _("PNG image (*.png)|*.png|"
                                       "JPEG image (*.jpg)|*.jpg|"
                                       "Windows bitmap (*.bmp)|*.bmp|"
                                       "X pixmap (*.xpm)|*.xpm"),
                                     wxSAVE | wxOVERWRITE_PROMPT);
      if (file.Length())
      {
        m_console->CopyToFile(file);
        m_lastPath = wxPathOnly(file);
      }
    }
    break;
  case menu_unfold:
    m_console->UnfoldAll();
    break;
#if defined (__WXMSW__) || defined (__WXGTK20__)
  case tb_delete:
#endif
  case menu_delete_selection:
  case popid_delete:
    if (m_console->CanDeleteSelection())
    {
      ResetTitle(false);
      m_console->DeleteSelection();
      m_console->Recalculate(false);
      m_console->Refresh();
    }
    break;
  case menu_select_last:
    if (m_console->SelectLastInput())
    {
      m_console->SetFocus();
      return ;
    }
    break;
  case menu_inc_fontsize:
    {
      int fontSize = 12;
      wxConfig::Get()->Read(wxT("fontSize"), &fontSize);
      if (fontSize < 20)
      {
        fontSize++;
        wxConfig::Get()->Write(wxT("fontSize"), fontSize);
        m_console->Recalculate(false);
        m_console->Refresh();
      }
    }
    break;
  case menu_dec_fontsize:
    {
      int fontSize = 12;
      wxConfig::Get()->Read(wxT("fontSize"), &fontSize);
      if (fontSize > 8)
      {
        fontSize--;
        wxConfig::Get()->Write(wxT("fontSize"), fontSize);
        m_console->Recalculate(false);
        m_console->Refresh();
      }
    }
    break;
  }
  m_inputLine->SetFocus();
}

void wxMaxima::MaximaMenu(wxCommandEvent& event)
{
  wxString expr = GetDefaultEntry();
  wxString cmd;
  wxString b = wxT("\\");
  wxString f = wxT("/");
  switch (event.GetId())
  {
  case menu_restart_id:
    m_currentFile = wxEmptyString;
    ResetTitle(true);
    StartMaxima();
    break;
  case menu_soft_restart:
    SendMaxima(wxT("kill(all);"));
    break;
  case menu_functions:
    SendMaxima(wxT("functions;"));
    break;
  case menu_variables:
    SendMaxima(wxT("values;"));
    break;
  case menu_display:
    {
      wxString choices[] =
        {
          wxT("xml"), wxT("ascii"), wxT("none")
        };
      wxString choice = wxGetSingleChoice(
                          _("Select math display algorithm"),
                          _("Display algorithm"),
                          3,
                          choices,
                          this
                        );
      if (choice.Length())
      {
        cmd = wxT("set_display('") + choice + wxT(")$");
        SendMaxima(cmd, false);
      }
    }
    break;
  case menu_texform:
    cmd = wxT("tex(") + expr + wxT(")$");
    SendMaxima(cmd);
    break;
  case menu_time:
    cmd = wxT("if showtime#false then showtime:false else showtime:all$");
    SendMaxima(cmd, false);
    break;
  case menu_fun_def:
    cmd = GetTextFromUser(_("Show the definition of function:"),
                          _("Function"), wxEmptyString, this);
    if (cmd.Length())
    {
      cmd = wxT("fundef(") + cmd + wxT(");");
      SendMaxima(cmd);
    }
    break;
  case menu_add_path:
    {
      if (m_lastPath.Length() == 0)
        m_lastPath = wxGetHomeDir();
      wxString dir = wxDirSelector(_("Add dir to path:"), m_lastPath);
      if (dir.Length())
      {
        m_lastPath = dir;
#if defined (__WXMSW__)
        dir.Replace(wxT("\\"), wxT("/"));
#endif
        cmd = wxT("file_search_maxima : cons(sconcat(\"") + dir +
              wxT("/###.{lisp,mac,mc}\"), file_search_maxima)$");
        SendMaxima(cmd);
      }
    }
    break;
  case menu_clear_var:
    cmd = GetTextFromUser(_("Delete variable(s):"), _("Delete"),
                          wxT("all"), this);
    if (cmd.Length())
    {
      cmd = wxT("remvalue(") + cmd + wxT(");");
      SendMaxima(cmd);
    }
    break;
  case menu_clear_fun:
    cmd = GetTextFromUser(_("Delete function(s):"), _("Delete"),
                          wxT("all"), this);
    if (cmd.Length())
    {
      cmd = wxT("remfunction(") + cmd + wxT(");");
      SendMaxima(cmd);
    }
    break;
  case menu_subst:
  case button_subst:
    {
      SubstituteWiz *wiz = new SubstituteWiz(this, -1, _("Substitute"));
      wiz->SetValue(expr);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wiz->GetValue();
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case menu_long_input:
  case button_long_input:
    {
      TextInput *wiz = new TextInput(this);
      if (expr.StartsWith(wxT("<ml>")))
      {
        expr = expr.SubString(4, expr.Length());
        expr.Replace(wxT("<nl>"), wxT("\n"));
      }
      wiz->SetValue(expr);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wiz->GetValue();
        val.Trim();
        val.Trim(false);
        if (!m_inLispMode && val.Last() != ';' && val.Last() != '$')
          val += wxT("$");
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  default:
    break;
  }
  m_inputLine->SetFocus();
}

void wxMaxima::EquationsMenu(wxCommandEvent& event)
{
  wxString expr = GetDefaultEntry();
  wxString cmd;
  switch (event.GetId())
  {
  case menu_allroots:
    cmd = wxT("allroots(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_realroots:
    cmd = wxT("realroots(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case button_solve:
  case menu_solve:
    {
      Gen2Wiz *wiz = new Gen2Wiz(_("Solve equation(s):"), _("for variable(s):"),
                                 expr, wxT("x"), this, -1, _("Solve"), true);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        cmd = wxT("solve([") + wiz->GetValue1() + wxT("], [") +
              wiz->GetValue2() + wxT("]);");
        SendMaxima(cmd);
      }
      wiz->Destroy();
    }
    break;
  case menu_solve_num:
    {
      if (expr.StartsWith(wxT("%")))
        expr = wxT("''(") + expr + wxT(")");
      Gen4Wiz *wiz = new Gen4Wiz(_("Solve equation:"), _("in variable:"),
                                 _("lower bound:"), _("upper bound:"),
                                 expr, wxT("x"), wxT("-1"), wxT("1"),
                                 this, -1, _("Solve numerically"), true);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        cmd = wxT("find_root(") + wiz->GetValue1() + wxT(", ") +
              wiz->GetValue2() + wxT(", ") +
              wiz->GetValue3() + wxT(", ") +
              wiz->GetValue4() + wxT(");");
        SendMaxima(cmd);
      }
      wiz->Destroy();
    }
    break;
  case button_solve_ode:
  case menu_solve_ode:
    {
      Gen3Wiz *wiz = new Gen3Wiz(_("Equation:"), _("function:"), _("variable:"),
                                 expr, wxT("y"), wxT("x"),
                                 this, -1, _("Solve ODE"));
      wiz->setValue(expr);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wxT("ode2(") + wiz->GetValue1() + wxT(", ") +
                       wiz->GetValue2() + wxT(", ") + wiz->GetValue3() + wxT(");");
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case menu_ivp_1:
    {
      Gen3Wiz *wiz = new Gen3Wiz(_("Solution:"), _("At point:"), _("the value is:"),
                                 expr, wxT("x="), wxT("y="),
                                 this, -1, _("IC1"), true);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wxT("ic1(") + wiz->GetValue1() + wxT(", ") +
                       wiz->GetValue2() + wxT(", ") + wiz->GetValue3() + wxT(");");
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case menu_ivp_2:
    {
      Gen4Wiz *wiz = new Gen4Wiz(_("Solution:"), _("At point:"),
                                 _("the value is:"), _("the derivative is:"),
                                 expr, wxT("x="), wxT("y="), wxT("'diff(y,x)="),
                                 this, -1, _("IC2"), true);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wxT("ic2(") + wiz->GetValue1() + wxT(", ") +
                       wiz->GetValue2() + wxT(", ") + wiz->GetValue3() +
                       wxT(", ") + wiz->GetValue4() + wxT(");");
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case menu_bvp:
    {
      BC2Wiz *wiz = new BC2Wiz(this, -1, _("BC2"));
      wiz->SetValue(expr);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wiz->GetValue();
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case menu_eliminate:
    {
      Gen2Wiz *wiz = new Gen2Wiz(_("From equations:"),
                                 _("eliminate variables:"), expr, wxEmptyString,
                                 this, -1, _("Eliminate"), true);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        cmd = wxT("eliminate([") + wiz->GetValue1() + wxT("],[")
              + wiz->GetValue2() + wxT("]);");
        SendMaxima(cmd);
      }
      wiz->Destroy();
    }
    break;
  case menu_solve_algsys:
    {
      wxString sz = GetTextFromUser(_("Number of equations:"),
                                    _("Solve algebraic system"),
                                    wxT("3"), this);
      if (sz.Length() == 0)
        return ;
      long isz;
      if (!sz.ToLong(&isz) || isz <= 0)
      {
        wxMessageBox(_("Not a valid number of equations!"), _("Error!"),
                     wxOK | wxICON_ERROR);
        return ;
      }
      SysWiz *wiz = new SysWiz(this, -1, _("Solve algebraic system"), isz);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        cmd = wxT("algsys") + wiz->GetValue();
        SendMaxima(cmd);
      }
      wiz->Destroy();
    }
    break;
  case menu_solve_lin:
    {
      wxString sz = GetTextFromUser(_("Number of equations:"),
                                    _("Solve linear system"),
                                    wxT("3"), this);
      if (sz.Length() == 0)
        return ;
      long isz;
      if (!sz.ToLong(&isz) || isz <= 0)
      {
        wxMessageBox(_("Not a valid number of equations!"), _("Error!"),
                     wxOK | wxICON_ERROR);
        return ;
      }
      SysWiz *wiz = new SysWiz(this, -1, _("Solve linear system"), isz);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        cmd = wxT("linsolve") + wiz->GetValue();
        SendMaxima(cmd);
      }
      wiz->Destroy();
    }
    break;
  case menu_solve_de:
    {
      Gen2Wiz *wiz = new Gen2Wiz(_("Solve equation(s):"), _("for function(s):"),
                                 expr, wxT("y(x)"),
                                 this, -1, _("Solve ODE"));
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        cmd = wxT("desolve([") + wiz->GetValue1() + wxT("],[")
              + wiz->GetValue2() + wxT("]);");
        SendMaxima(cmd);
      }
      wiz->Destroy();
    }
    break;
  case menu_atvalue:
    {
      Gen3Wiz *wiz = new Gen3Wiz(_("Expression:"), _("at point:"),
                                 _("has value:"), expr, wxT("x=0"), wxT("0"),
                                 this, -1, _("Atvalue"));
      wiz->setValue(expr);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wxT("atvalue(") + wiz->GetValue1() + wxT(", ")
                       + wiz->GetValue2() +
                       wxT(", ") + wiz->GetValue3() + wxT(");");
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  default:
    break;
  }
  m_inputLine->SetFocus();
}

void wxMaxima::AlgebraMenu(wxCommandEvent& event)
{
  wxString expr = GetDefaultEntry();
  wxString cmd;
  switch (event.GetId())
  {
  case menu_invert_mat:
    cmd = wxT("invert(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_determinant:
    cmd = wxT("determinant(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_eigen:
    cmd = wxT("eigenvalues(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_eigvect:
    cmd = wxT("eigenvectors(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_adjoint_mat:
    cmd = wxT("adjoint(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_transpose:
    cmd = wxT("transpose(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_map_mat:
    {
      Gen2Wiz *wiz = new Gen2Wiz(_("Map function:"), _("to matrix:"),
                                 wxEmptyString, expr,
                                 this, -1, _("Matrix map"));
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        cmd = wxT("matrixmap(") + wiz->GetValue1() + wxT(", ")
              + wiz->GetValue2() + wxT(");");
        SendMaxima(cmd);
      }
      wiz->Destroy();
    }
    break;
  case menu_enter_mat:
    {
      MatDim *wiz = new MatDim(this, -1, _("Matrix"));
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        if (expr.Length() && expr != wxT("%"))
          cmd = expr;
        long w, h;
        int type = wiz->GetMatrixType();
        if (!(wiz->GetValue2()).ToLong(&w) ||
                !(wiz->GetValue1()).ToLong(&h) ||
                w <= 0 || h <= 0)
        {
          wxMessageBox(_("Not a valid matrix dimension!"), _("Error!"),
                       wxOK | wxICON_ERROR);
          return ;
        }
        if (w != h)
          type = MATRIX_GENERAL;
        MatWiz *mwiz = new MatWiz(this, -1, _("Enter matrix"),
                                  type, w, h);
        mwiz->Centre(wxBOTH);
        if (mwiz->ShowModal() == wxID_OK)
        {
          cmd += mwiz->GetValue();
          SendMaxima(cmd);
        }
        mwiz->Destroy();
      }
      wiz->Destroy();
    }
    break;
  case menu_cpoly:
    {
      Gen2Wiz *wiz = new Gen2Wiz(_("Char poly of:"), _("in variable:"),
                                 expr, wxT("x"),
                                 this, -1, _("Char poly"));
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        cmd = wxT("charpoly(") + wiz->GetValue1() + wxT(", ")
              + wiz->GetValue2() + wxT("), expand;");
        SendMaxima(cmd);
      }
      wiz->Destroy();
    }
    break;
  case menu_gen_mat:
    {
      Gen3Wiz *wiz = new Gen3Wiz(_("From array:"), _("width:"), _("height:"),
                                 expr, wxT("3"), wxT("3"),
                                 this, -1, _("Generate Matrix"));
      wiz->setValue(expr);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wxT("genmatrix(") + wiz->GetValue1() +
                       wxT(", ") + wiz->GetValue2() +
                       wxT(", ") + wiz->GetValue3() + wxT(");");
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case button_map:
  case menu_map:
    {
      Gen2Wiz *wiz = new Gen2Wiz(_("Map function:"), _("to list:"),
                                 wxEmptyString, expr,
                                 this, -1, _("Map"));
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        cmd = wxT("map(") + wiz->GetValue1() + wxT(", ") + wiz->GetValue2() +
              wxT(");");
        SendMaxima(cmd);
      }
      wiz->Destroy();
    }
    break;
  case menu_make_list:
    {
      Gen4Wiz *wiz = new Gen4Wiz(_("from expression:"), _("by variable:"),
                                 _("from:"), _("to:"),
                                 expr, wxT("k"), wxT("1"), wxT("10"),
                                 this, -1, _("Make list"));
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        cmd = wxT("makelist(") + wiz->GetValue1() + wxT(", ") +
              wiz->GetValue2() + wxT(", ") +
              wiz->GetValue3() + wxT(", ") +
              wiz->GetValue4() + wxT(");");
        SendMaxima(cmd);
      }
      wiz->Destroy();
    }
    break;
  case menu_apply:
    {
      Gen2Wiz *wiz = new Gen2Wiz(_("Apply function:"), _("to list:"),
                                 wxT("\"+\""), expr,
                                 this, -1, _("Apply"), true);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        cmd = wxT("apply(") + wiz->GetValue1() + wxT(", ")
              + wiz->GetValue2() + wxT(");");
        SendMaxima(cmd);
      }
      wiz->Destroy();
    }
    break;
  default:
    break;
  }
  m_inputLine->SetFocus();
}

void wxMaxima::SimplifyMenu(wxCommandEvent& event)
{
  wxString expr = GetDefaultEntry();
  wxString cmd;
  switch (event.GetId())
  {
  case menu_nouns:
    cmd = wxT("ev(") + expr + wxT(", nouns);");
    SendMaxima(cmd);
    break;
  case button_ratsimp:
  case menu_ratsimp:
    cmd = wxT("ratsimp(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case button_radcan:
  case menu_radsimp:
    cmd = wxT("radcan(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_to_fact:
    cmd = wxT("makefact(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_to_gamma:
    cmd = wxT("makegamma(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_factcomb:
    cmd = wxT("factcomb(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_factsimp:
    cmd = wxT("minfactorial(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_logcontract:
    cmd = wxT("logcontract(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_logexpand:
    cmd = expr + wxT(", logexpand=super;");
    SendMaxima(cmd);
    break;
  case button_expand:
  case menu_expand:
    cmd = wxT("expand(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case button_factor:
  case menu_factor:
    cmd = wxT("factor(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_gfactor:
    cmd = wxT("gfactor(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case button_trigreduce:
  case menu_trigreduce:
    cmd = wxT("trigreduce(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case button_trigsimp:
  case menu_trigsimp:
    cmd = wxT("trigsimp(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case button_trigexpand:
  case menu_trigexpand:
    cmd = wxT("trigexpand(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_trigrat:
    cmd = wxT("trigrat(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case button_rectform:
  case menu_rectform:
    cmd = wxT("rectform(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_polarform:
    cmd = wxT("polarform(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_demoivre:
    cmd = wxT("demoivre(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_exponentialize:
    cmd = wxT("exponentialize(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_realpart:
    cmd = wxT("realpart(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_imagpart:
    cmd = wxT("imagpart(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_talg:
    cmd = wxT("algebraic : not(algebraic);");
    SendMaxima(cmd);
    break;
  case menu_tellrat:
    cmd = GetTextFromUser(_("Enter an equation for rational simplification:"),
                          _("Tellrat"), wxEmptyString, this);
    if (cmd.Length())
    {
      cmd = wxT("tellrat(") + cmd + wxT(");");
      SendMaxima(cmd);
    }
    break;
  case menu_modulus:
    cmd = GetTextFromUser(_("Calculate modulus:"),
                          _("Modulus"), wxT("false"), this);
    if (cmd.Length())
    {
      cmd = wxT("modulus : ") + cmd + wxT(";");
      SendMaxima(cmd);
    }
    break;
  default:
    break;
  }
  m_inputLine->SetFocus();
}

void wxMaxima::CalculusMenu(wxCommandEvent& event)
{
  wxString expr = GetDefaultEntry();
  wxString cmd;
  switch (event.GetId())
  {
  case menu_change_var:
    {
      Gen4Wiz *wiz = new Gen4Wiz(_("Integral/sum:"), _("old variable:"),
                                 _("new variable:"), _("equation:"),
                                 expr, wxT("x"), wxT("y"), wxT("y=x"),
                                 this, -1, _("Change variable"), true);
      wiz->setValue(expr);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wxT("changevar(") + wiz->GetValue1() + wxT(", ") +
                       wiz->GetValue4() + wxT(", ") + wiz->GetValue3() + wxT(", ") +
                       wiz->GetValue2() + wxT(");");
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case menu_pade:
    {
      Gen3Wiz *wiz = new Gen3Wiz(_("Taylor series:"), _("num deg:"),
                                 _("denom deg:"), expr, wxT("4"), wxT("4"),
                                 this, -1, _("Pade approximation"));
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wxT("pade(") + wiz->GetValue1() + wxT(", ") +
                       wiz->GetValue2() + wxT(", ") + wiz->GetValue3() + wxT(");");
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case menu_continued_fraction:
    cmd += wxT("cfdisrep(cf(") + expr + wxT("));");
    SendMaxima(cmd);
    break;
  case menu_lcm:
    {
      Gen2Wiz *wiz = new Gen2Wiz(_("Polynomial 1:"), _("Polynomial 2:"),
                                 wxEmptyString, wxEmptyString,
                                 this, -1, _("LCM"), true);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        cmd = wxT("lcm(") + wiz->GetValue1() + wxT(", ")
              + wiz->GetValue2() + wxT(");");
        SendMaxima(cmd);
      }
      wiz->Destroy();
    }
    break;
  case menu_gcd:
    {
      Gen2Wiz *wiz = new Gen2Wiz(_("Polynomial 1:"), _("Polynomial 2:"),
                                 wxEmptyString, wxEmptyString,
                                 this, -1, _("GCD"), true);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        cmd = wxT("gcd(") + wiz->GetValue1() + wxT(", ")
              + wiz->GetValue2() + wxT(");");
        SendMaxima(cmd);
      }
      wiz->Destroy();
    }
    break;
  case menu_divide:
    {
      Gen2Wiz *wiz = new Gen2Wiz(_("Polynomial 1:"), _("Polynomial 2:"),
                                 expr, wxEmptyString,
                                 this, -1, _("Divide"), true);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        cmd = wxT("divide(") + wiz->GetValue1() + wxT(", ") +
              wiz->GetValue2() + wxT(");");
        SendMaxima(cmd);
      }
      wiz->Destroy();
    }
    break;
  case menu_partfrac:
    {
      Gen2Wiz *wiz = new Gen2Wiz(_("Expression:"), _("in variable:"),
                                 expr, wxT("n"),
                                 this, -1, _("Partial fractions"));
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        cmd = wxT("partfrac(") + wiz->GetValue1() + wxT(", ")
              + wiz->GetValue2() + wxT(");");
        SendMaxima(cmd);
      }
      wiz->Destroy();
    }
    break;
  case menu_risch:
    {
      Gen2Wiz *wiz = new Gen2Wiz(_("Expression:"), _("by variable:"),
                                 expr, wxT("x"),
                                 this, -1, _("Integrate (risch)"));
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        cmd = wxT("risch(") + wiz->GetValue1() + wxT(", ")
              + wiz->GetValue2() + wxT(");");
        SendMaxima(cmd);
      }
      wiz->Destroy();
    }
    break;
  case button_integrate:
  case menu_integrate:
    {
      IntegrateWiz *wiz = new IntegrateWiz(this, -1, _("Integrate"));
      wiz->SetValue(expr);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wiz->GetValue();
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case menu_laplace:
    {
      Gen3Wiz *wiz = new Gen3Wiz(_("Expression:"), _("change var:"),
                                 _("to:"), expr, wxT("t"), wxT("s"),
                                 this, -1, _("Laplace"));
      wiz->setValue(expr);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wxT("laplace(") + wiz->GetValue1() + wxT(", ")
                       + wiz->GetValue2() +
                       wxT(", ") + wiz->GetValue3() + wxT(");");
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case menu_ilt:
    {
      Gen3Wiz *wiz = new Gen3Wiz(_("Expression:"), _("change var:"),
                                 _("to:"), expr, wxT("s"), wxT("t"),
                                 this, -1, _("Inverse Laplace"));
      wiz->setValue(expr);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wxT("ilt(") + wiz->GetValue1() + wxT(", ") +
                       wiz->GetValue2() + wxT(", ") + wiz->GetValue3() + wxT(");");
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case button_diff:
  case menu_diff:
    {
      Gen3Wiz *wiz = new Gen3Wiz(_("Expression:"), _("in variable:"),
                                 _("times:"), expr, wxT("x"), wxT("1"),
                                 this, -1, _("Differentiate"));
      wiz->setValue(expr);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wxT("diff(") + wiz->GetValue1() + wxT(", ") +
                       wiz->GetValue2();
        if (wiz->GetValue3() != wxT("1"))
          val += wxT(", ") + wiz->GetValue3();
        val += wxT(");");
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case button_taylor:
  case menu_series:
    {
      SeriesWiz *wiz = new SeriesWiz(this, -1, _("Series"));
      wiz->SetValue(expr);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wiz->GetValue();
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case button_limit:
  case menu_limit:
    {
      LimitWiz *wiz = new LimitWiz(this, -1, _("Limit"));
      wiz->SetValue(expr);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wiz->GetValue();
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case button_sum:
  case menu_sum:
    {
      SumWiz *wiz = new SumWiz(this, -1, _("Sum"));
      wiz->SetValue(expr);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wiz->GetValue();
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case button_product:
  case menu_product:
    {
      Gen4Wiz *wiz = new Gen4Wiz(_("Product:"), _("by variable:"), _("from:"),
                                 _("to:"), expr, wxT("k"), wxT("1"), wxT("n"),
                                 this, -1, _("Product"));
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        cmd = wxT("product(") + wiz->GetValue1() + wxT(", ") +
              wiz->GetValue2() + wxT(", ") +
              wiz->GetValue3() + wxT(", ") +
              wiz->GetValue4() + wxT(");");
        SendMaxima(cmd);
      }
      wiz->Destroy();
    }
    break;
  default:
    break;
  }
  m_inputLine->SetFocus();
}

void wxMaxima::PlotMenu(wxCommandEvent& event)
{
  wxString expr = GetDefaultEntry();
  wxString cmd;
  switch (event.GetId())
  {
  case button_plot3:
  case gp_plot3:
    {
      Plot3DWiz *wiz = new Plot3DWiz(this, -1, _("Plot 3D"));
      wiz->SetValue(expr);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wiz->GetValue();
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case button_plot2:
  case gp_plot2:
    {
      Plot2DWiz *wiz = new Plot2DWiz(this, -1, _("Plot 2D"));
      wiz->SetValue(expr);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wiz->GetValue();
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case menu_plot_format:
    {
      wxString format = GetTextFromUser(_("Enter new plot format:"),
                                        _("Plot format"),
                                        wxT("gnuplot"), this);
      if (format.Length())
      {
        SendMaxima(wxT("set_plot_option(['plot_format, '") + format +
                   wxT("])$"));
      }
    }
  default:
    break;
  }
  m_inputLine->SetFocus();
}

void wxMaxima::NumericalMenu(wxCommandEvent& event)
{
  wxString expr = GetDefaultEntry();
  wxString cmd;
  switch (event.GetId())
  {
  case menu_to_float:
    cmd = wxT("float(") + expr + wxT("), numer;");
    SendMaxima(cmd);
    break;
  case menu_to_bfloat:
    cmd = wxT("bfloat(") + expr + wxT(");");
    SendMaxima(cmd);
    break;
  case menu_num_out:
    cmd = wxT("if numer#false then numer:false else numer:true;");
    SendMaxima(cmd, false);
    break;
  case menu_set_precision:
    cmd = GetTextFromUser(_("Enter new precision:"), _("Precision"),
                          wxT("16"), this);
    if (cmd.Length())
    {
      cmd = wxT("fpprec : ") + cmd + wxT(";");
      SendMaxima(cmd);
    }
    break;
  default:
    break;
  }
  m_inputLine->SetFocus();
}

void wxMaxima::HelpMenu(wxCommandEvent& event)
{
  wxString expr = GetDefaultEntry();
  wxString cmd;
  switch (event.GetId())
  {
  case wxID_ABOUT:
    wxMessageBox(wxString::Format(
                   _("wxMaxima is a wxWidgets interface for the\n"
                     "computer algebra system MAXIMA.\n"
                     "\nVersion: %s."
                     "\nLicense: GPL.\n"
                     "\n%s\n%s"),
                   wxT(VERSION),
                   wxT("http://wxmaxima.sourceforge.net/"),
                   wxT("http://maxima.sourceforge.net/")),
                 _("About wxMaxima"), wxOK | wxICON_INFORMATION);
    break;
  case wxID_HELP:
#if defined (__WXMSW__) || defined (__WXGTK20__)
  case tb_help:
#endif
    ShowHelp(expr);
    return ;
  case menu_describe:
    if (expr == wxT("%"))
      cmd = GetTextFromUser(_("Show the description of command/variable:"),
                            _("Describe"), wxEmptyString, this);
    else
    {
      cmd = expr;
      m_inputLine->SetValue(wxEmptyString);
    }
    if (cmd.Length())
      ShowHelp(cmd);
    break;
  case menu_example:
    if (expr == wxT("%"))
      cmd = GetTextFromUser(_("Show an example for the command:"), _("Example"),
                            wxEmptyString, this);
    else
    {
      cmd = expr;
      m_inputLine->SetValue(wxEmptyString);
    }
    if (cmd.Length())
    {
      cmd = wxT("example(") + cmd + wxT(");");
      SendMaxima(cmd, false);
    }
    break;
  case menu_apropos:
    if (expr == wxT("%"))
      cmd = GetTextFromUser(_("Show all commands similar to:"), _("Apropos"),
                            wxEmptyString, this);
    else
    {
      cmd = expr;
      m_inputLine->SetValue(wxEmptyString);
    }
    if (cmd.Length())
    {
      cmd = wxT("apropos(\"") + cmd + wxT("\");");
      SendMaxima(cmd, false);
    }
    break;
  case menu_show_tip:
    ShowTip(true);
    break;
  case menu_build_info:
    SendMaxima(wxT("build_info()$"), false);
    break;
  case menu_bug_report:
    SendMaxima(wxT("bug_report()$"), false);
    break;
  default:
    break;
  }
}

void wxMaxima::OnClose(wxCloseEvent& event)
{
  wxConfig *config = (wxConfig *)wxConfig::Get();
  wxSize size = GetSize();
  wxPoint pos = GetPosition();
  bool maximized = IsMaximized();
  config->Write(wxT("pos-x"), pos.x);
  config->Write(wxT("pos-y"), pos.y);
  config->Write(wxT("pos-w"), size.GetWidth());
  config->Write(wxT("pos-h"), size.GetHeight());
  if (maximized)
    config->Write(wxT("pos-max"), 1);
  else
    config->Write(wxT("pos-max"), 0);
  if (m_lastPath.Length() > 0)
    config->Write(wxT("lastPath"), m_lastPath);
  m_closing = true;
  CleanUp();
  Destroy();
}

void wxMaxima::PopupMenu(wxCommandEvent& event)
{
  wxString selection = m_console->GetString();
  switch (event.GetId())
  {
  case popid_copy:
    if (m_console->CanCopy(true))
      m_console->Copy();
    break;
  case popid_copy_tex:
    if (m_console->CanCopy(true))
      m_console->CopyTeX();
    break;
  case popid_cut:
    if (m_console->CanCopy(true))
      m_console->CutToClipboard();
    break;
  case popid_paste:
    m_console->PasteFromClipboard();
    break;
  case popid_select_all:
    m_console->SelectAll();
    break;
  case popid_copy_text:
    if (m_console->CanCopy())
      m_console->Copy(true);
    break;
  case popid_copy_image:
    if (m_console->CanCopy())
      m_console->CopyBitmap();
    break;
  case popid_simplify:
    SendMaxima(wxT("ratsimp(") + selection + wxT(");"));
    break;
  case popid_expand:
    SendMaxima(wxT("expand(") + selection + wxT(");"));
    break;
  case popid_factor:
    SendMaxima(wxT("factor(") + selection + wxT(");"));
    break;
  case popid_solve:
    {
      Gen2Wiz *wiz = new Gen2Wiz(_("Solve equation(s):"), _("for variable(s):"),
                                 selection, wxT("x"), this, -1, _("Solve"), true);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString cmd = wxT("solve([") + wiz->GetValue1() + wxT("], [") +
                       wiz->GetValue2() + wxT("]);");
        SendMaxima(cmd);
      }
      wiz->Destroy();
    }
    break;
  case popid_solve_num:
    {
      Gen4Wiz *wiz = new Gen4Wiz(_("Solve equation:"), _("in variable:"),
                                 _("lower bound:"), _("upper bound:"),
                                 selection, wxT("x"), wxT("-1"), wxT("1"),
                                 this, -1, _("Solve numerically"), true);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString cmd = wxT("find_root(") + wiz->GetValue1() + wxT(", ") +
                       wiz->GetValue2() + wxT(", ") +
                       wiz->GetValue3() + wxT(", ") +
                       wiz->GetValue4() + wxT(");");
        SendMaxima(cmd);
      }
      wiz->Destroy();
    }
    break;
  case popid_integrate:
    {
      IntegrateWiz *wiz = new IntegrateWiz(this, -1, _("Integrate"));
      wiz->SetValue(selection);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wiz->GetValue();
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case popid_diff:
    {
      Gen3Wiz *wiz = new Gen3Wiz(_("Expression:"), _("in variable:"),
                                 _("times:"), selection, wxT("x"), wxT("1"),
                                 this, -1, _("Differentiate"));
      wiz->setValue(selection);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wxT("diff(") + wiz->GetValue1() + wxT(", ") +
                       wiz->GetValue2();
        if (wiz->GetValue3() != wxT("1"))
          val += wxT(", ") + wiz->GetValue3();
        val += wxT(");");
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case popid_subst:
    {
      SubstituteWiz *wiz = new SubstituteWiz(this, -1, _("Substitute"));
      wiz->SetValue(selection);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wiz->GetValue();
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case popid_plot2d:
    {
      Plot2DWiz *wiz = new Plot2DWiz(this, -1, _("Plot 2D"));
      wiz->SetValue(selection);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wiz->GetValue();
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case popid_plot3d:
    {
      Plot3DWiz *wiz = new Plot3DWiz(this, -1, _("Plot 3D"));
      wiz->SetValue(selection);
      wiz->Centre(wxBOTH);
      if (wiz->ShowModal() == wxID_OK)
      {
        wxString val = wiz->GetValue();
        SendMaxima(val);
      }
      wiz->Destroy();
    }
    break;
  case popid_float:
    SendMaxima(wxT("float(") + selection + wxT("), numer;"));
    break;
  }
}

void wxMaxima::HandleCellEvent(wxCommandEvent& event)
{
  ResetTitle(false);
  MathCell* tmp = m_console->GetActiveCell();

  if (tmp == NULL)
    return ;

  if (tmp->GetType() == MC_TYPE_INPUT)
    tmp->AddEnding();

  m_console->SetSelection(tmp);

  m_console->SetActiveCell(NULL);

  if (event.GetId() == deactivate_cell_ok && tmp->GetType() == MC_TYPE_INPUT)
  {
    wxCommandEvent ev(wxEVT_COMMAND_MENU_SELECTED, popid_reeval);
    ProcessEvent(ev);
  }
  else
    m_console->Refresh();
}

void wxMaxima::EditInputMenu(wxCommandEvent& event)
{
  if (!m_console->CanEdit())
    return ;

  MathCell* tmp = m_console->GetSelectionStart();

  if (tmp == NULL)
    return ;

  m_console->SetActiveCell(tmp);
  m_console->SetSelection(NULL);

  m_console->Refresh();
}

void wxMaxima::ReEvaluate(wxCommandEvent& event)
{
  if (!m_console->CanEdit())
    return ;

  ResetTitle(false);

  MathCell* beginInput = m_console->GetSelectionStart();

  if (beginInput == NULL)
    return ;

  if (beginInput->GetType() == MC_TYPE_INPUT)
  {
    wxString text = m_console->GetString(true);

    m_inInsertMode = true;

    beginInput = beginInput->m_previous;

    int x, y;
    m_console->GetViewStart(&x, &y);
    m_console->SetScrollTo(y);

    m_console->SetSelection(beginInput);
    m_console->DeleteSelection(false);

    m_console->SetInsertPoint(beginInput);

    beginInput->SetValue(m_lastPrompt);

    SendMaxima(text, true, true, true, false);
  }
}

void wxMaxima::PrependCell(wxCommandEvent& event)
{
  if (!m_console->SelectPrompt())
    return ;

  MathCell* prompt = m_console->GetSelectionStart();

  int x, y;
  m_console->GetViewStart(&x, &y);
  m_console->SetScrollTo(y);

  m_console->SetInsertPoint(prompt);

  DoRawConsoleAppend(prompt->GetValue(), MC_TYPE_MAIN_PROMPT);
  if (event.GetId() == popid_insert_input || event.GetId() == menu_insert_input
#if defined (__WXMSW__) || defined (__WXGTK20__)
      || event.GetId() == tb_insert_input
#endif
  )
  {
    prompt->SetValue(m_newInput);
    prompt->ResetData();
    m_console->Recalculate(false);
  }
  else
  {
    prompt->SetValue(m_commentPrefix);
    prompt->ResetData();
    m_console->Recalculate(false);
  }

  m_inInsertMode = true;

  m_console->SetInsertPoint(prompt);
  switch (event.GetId())
  {
  case popid_insert_input:
  case menu_insert_input:
#if defined (__WXMSW__) || defined (__WXGTK20__)
  case tb_insert_input:
#endif
    DoRawConsoleAppend(wxEmptyString, MC_TYPE_INPUT, false);
    break;
  case menu_add_comment:
  case popid_add_comment:
#if defined (__WXMSW__) || defined (__WXGTK20__)
  case tb_insert_text:
#endif
    DoRawConsoleAppend(wxEmptyString, MC_TYPE_COMMENT, true);
    break;
  case menu_add_title:
    DoRawConsoleAppend(wxEmptyString, MC_TYPE_TITLE, true);
    break;
  case menu_add_section:
    DoRawConsoleAppend(wxEmptyString, MC_TYPE_SECTION, true);
    break;
  }

  m_console->SetInsertPoint(NULL);

  prompt = prompt->m_nextToDraw;

  m_console->SetScrollTo(-1);
  m_console->SetActiveCell(prompt);
  m_console->SetFocus();
  m_console->Refresh();

  ResetTitle(false);
  m_inInsertMode = false;
}

void wxMaxima::ResetTitle(bool saved)
{
  if (m_currentFile.Length() == 0)
    SetTitle(wxString::Format(_("wxMaxima %s "), wxT(VERSION)) + _("[ unsaved ]"));
  else if (m_fileSaved != saved)
  {
    m_fileSaved = saved;
    wxString name, ext;
    wxFileName::SplitPath(m_currentFile, NULL, NULL, &name, &ext);
    if (m_fileSaved)
      SetTitle(wxString::Format(_("wxMaxima %s "), wxT(VERSION)) +
               wxT(" [ ") + name + wxT(".") + ext + wxT(" ]"));
    else
      SetTitle(wxString::Format(_("wxMaxima %s "), wxT(VERSION)) +
               wxT(" [ ") + name + wxT(".") + ext + wxT("* ]"));
  }
}

BEGIN_EVENT_TABLE(wxMaxima, wxFrame)
  EVT_MENU(popid_copy, wxMaxima::PopupMenu)
  EVT_MENU(popid_copy_text, wxMaxima::PopupMenu)
  EVT_MENU(popid_copy_image, wxMaxima::PopupMenu)
  EVT_MENU(popid_delete, wxMaxima::EditMenu)
  EVT_MENU(popid_simplify, wxMaxima::PopupMenu)
  EVT_MENU(popid_factor, wxMaxima::PopupMenu)
  EVT_MENU(popid_expand, wxMaxima::PopupMenu)
  EVT_MENU(popid_solve, wxMaxima::PopupMenu)
  EVT_MENU(popid_solve_num, wxMaxima::PopupMenu)
  EVT_MENU(popid_subst, wxMaxima::PopupMenu)
  EVT_MENU(popid_plot2d, wxMaxima::PopupMenu)
  EVT_MENU(popid_plot3d, wxMaxima::PopupMenu)
  EVT_MENU(popid_diff, wxMaxima::PopupMenu)
  EVT_MENU(popid_integrate, wxMaxima::PopupMenu)
  EVT_MENU(popid_float, wxMaxima::PopupMenu)
  EVT_MENU(popid_copy_tex, wxMaxima::PopupMenu)
  EVT_TEXT_ENTER(input_line_id, wxMaxima::EnterCommand)
  EVT_BUTTON(button_integrate, wxMaxima::CalculusMenu)
  EVT_BUTTON(button_diff, wxMaxima::CalculusMenu)
  EVT_BUTTON(button_solve, wxMaxima::EquationsMenu)
  EVT_BUTTON(button_solve_ode, wxMaxima::EquationsMenu)
  EVT_BUTTON(button_sum, wxMaxima::CalculusMenu)
  EVT_BUTTON(button_expand, wxMaxima::SimplifyMenu)
  EVT_BUTTON(button_factor, wxMaxima::SimplifyMenu)
  EVT_BUTTON(button_taylor, wxMaxima::CalculusMenu)
  EVT_BUTTON(button_limit, wxMaxima::CalculusMenu)
  EVT_BUTTON(button_ratsimp, wxMaxima::SimplifyMenu)
  EVT_BUTTON(button_trigexpand, wxMaxima::SimplifyMenu)
  EVT_BUTTON(button_trigreduce, wxMaxima::SimplifyMenu)
  EVT_BUTTON(button_trigsimp, wxMaxima::SimplifyMenu)
  EVT_BUTTON(button_product, wxMaxima::CalculusMenu)
  EVT_BUTTON(button_long_input, wxMaxima::MaximaMenu)
  EVT_BUTTON(button_radcan, wxMaxima::SimplifyMenu)
  EVT_BUTTON(button_subst, wxMaxima::MaximaMenu)
  EVT_BUTTON(button_plot2, wxMaxima::PlotMenu)
  EVT_BUTTON(button_plot3, wxMaxima::PlotMenu)
  EVT_BUTTON(button_map, wxMaxima::AlgebraMenu)
  EVT_BUTTON(button_rectform, wxMaxima::SimplifyMenu)
  EVT_BUTTON(button_enter, wxMaxima::EnterCommand)
  EVT_MENU(menu_polarform, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_restart_id, wxMaxima::MaximaMenu)
  EVT_MENU(wxID_EXIT, wxMaxima::FileMenu)
  EVT_MENU(wxID_ABOUT, wxMaxima::HelpMenu)
  EVT_MENU(menu_save_id, wxMaxima::FileMenu)
  EVT_MENU(menu_save_as_id, wxMaxima::FileMenu)
  EVT_MENU(menu_load_id, wxMaxima::FileMenu)
  EVT_MENU(menu_monitor_file, wxMaxima::OnMonitorFile)
  EVT_MENU(menu_select_file, wxMaxima::FileMenu)
  EVT_MENU(menu_functions, wxMaxima::MaximaMenu)
  EVT_MENU(menu_variables, wxMaxima::MaximaMenu)
  EVT_MENU(wxID_PREFERENCES, wxMaxima::EditMenu)
  EVT_MENU(menu_sconsole_id, wxMaxima::FileMenu)
  EVT_MENU(menu_export_html, wxMaxima::FileMenu)
  EVT_MENU(wxID_HELP, wxMaxima::HelpMenu)
  EVT_MENU(menu_bug_report, wxMaxima::HelpMenu)
  EVT_MENU(menu_build_info, wxMaxima::HelpMenu)
  EVT_MENU(menu_interrupt_id, wxMaxima::Interrupt)
  EVT_MENU(menu_open_id, wxMaxima::FileMenu)
  EVT_MENU(menu_batch_id, wxMaxima::FileMenu)
  EVT_MENU(menu_ratsimp, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_radsimp, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_expand, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_factor, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_gfactor, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_trigsimp, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_trigexpand, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_trigreduce, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_rectform, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_demoivre, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_num_out, wxMaxima::NumericalMenu)
  EVT_MENU(menu_to_float, wxMaxima::NumericalMenu)
  EVT_MENU(menu_to_bfloat, wxMaxima::NumericalMenu)
  EVT_MENU(menu_exponentialize, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_invert_mat, wxMaxima::AlgebraMenu)
  EVT_MENU(menu_determinant, wxMaxima::AlgebraMenu)
  EVT_MENU(menu_eigen, wxMaxima::AlgebraMenu)
  EVT_MENU(menu_eigvect, wxMaxima::AlgebraMenu)
  EVT_MENU(menu_adjoint_mat, wxMaxima::AlgebraMenu)
  EVT_MENU(menu_transpose, wxMaxima::AlgebraMenu)
  EVT_MENU(menu_set_precision, wxMaxima::NumericalMenu)
  EVT_MENU(menu_talg, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_tellrat, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_modulus, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_allroots, wxMaxima::EquationsMenu)
  EVT_MENU(menu_realroots, wxMaxima::EquationsMenu)
  EVT_MENU(menu_solve, wxMaxima::EquationsMenu)
  EVT_MENU(menu_solve_num, wxMaxima::EquationsMenu)
  EVT_MENU(menu_solve_ode, wxMaxima::EquationsMenu)
  EVT_MENU(menu_map_mat, wxMaxima::AlgebraMenu)
  EVT_MENU(menu_enter_mat, wxMaxima::AlgebraMenu)
  EVT_MENU(menu_cpoly, wxMaxima::AlgebraMenu)
  EVT_MENU(menu_solve_lin, wxMaxima::EquationsMenu)
  EVT_MENU(menu_solve_algsys, wxMaxima::EquationsMenu)
  EVT_MENU(menu_eliminate, wxMaxima::EquationsMenu)
  EVT_MENU(menu_clear_var, wxMaxima::MaximaMenu)
  EVT_MENU(menu_clear_fun, wxMaxima::MaximaMenu)
  EVT_MENU(menu_ivp_1, wxMaxima::EquationsMenu)
  EVT_MENU(menu_ivp_2, wxMaxima::EquationsMenu)
  EVT_MENU(menu_bvp, wxMaxima::EquationsMenu)
  EVT_MENU(menu_bvp, wxMaxima::EquationsMenu)
  EVT_MENU(menu_fun_def, wxMaxima::MaximaMenu)
  EVT_MENU(menu_describe, wxMaxima::HelpMenu)
  EVT_MENU(menu_divide, wxMaxima::CalculusMenu)
  EVT_MENU(menu_gcd, wxMaxima::CalculusMenu)
  EVT_MENU(menu_lcm, wxMaxima::CalculusMenu)
  EVT_MENU(menu_continued_fraction, wxMaxima::CalculusMenu)
  EVT_MENU(menu_partfrac, wxMaxima::CalculusMenu)
  EVT_MENU(menu_risch, wxMaxima::CalculusMenu)
  EVT_MENU(menu_integrate, wxMaxima::CalculusMenu)
  EVT_MENU(menu_laplace, wxMaxima::CalculusMenu)
  EVT_MENU(menu_ilt, wxMaxima::CalculusMenu)
  EVT_MENU(menu_diff, wxMaxima::CalculusMenu)
  EVT_MENU(menu_series, wxMaxima::CalculusMenu)
  EVT_MENU(menu_limit, wxMaxima::CalculusMenu)
  EVT_MENU(menu_gen_mat, wxMaxima::AlgebraMenu)
  EVT_MENU(menu_map, wxMaxima::AlgebraMenu)
  EVT_MENU(menu_sum, wxMaxima::CalculusMenu)
  EVT_MENU(menu_example, wxMaxima::HelpMenu)
  EVT_MENU(menu_apropos, wxMaxima::HelpMenu)
  EVT_MENU(menu_show_tip, wxMaxima::HelpMenu)
  EVT_MENU(menu_trigrat, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_solve_de, wxMaxima::EquationsMenu)
  EVT_MENU(menu_atvalue, wxMaxima::EquationsMenu)
  EVT_MENU(menu_sum, wxMaxima::CalculusMenu)
  EVT_MENU(menu_product, wxMaxima::CalculusMenu)
  EVT_MENU(menu_change_var, wxMaxima::CalculusMenu)
  EVT_MENU(menu_make_list, wxMaxima::AlgebraMenu)
  EVT_MENU(menu_apply, wxMaxima::AlgebraMenu)
  EVT_MENU(menu_time, wxMaxima::MaximaMenu)
  EVT_MENU(menu_factsimp, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_factcomb, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_realpart, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_imagpart, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_nouns, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_logcontract, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_logexpand, wxMaxima::SimplifyMenu)
  EVT_MENU(gp_plot2, wxMaxima::PlotMenu)
  EVT_MENU(gp_plot3, wxMaxima::PlotMenu)
  EVT_MENU(menu_plot_format, wxMaxima::PlotMenu)
  EVT_MENU(menu_soft_restart, wxMaxima::MaximaMenu)
  EVT_MENU(menu_display, wxMaxima::MaximaMenu)
  EVT_MENU(menu_pade, wxMaxima::CalculusMenu)
  EVT_MENU(menu_add_path, wxMaxima::MaximaMenu)
  EVT_MENU(menu_clear_screen, wxMaxima::EditMenu)
  EVT_MENU(menu_copy_from_console, wxMaxima::EditMenu)
  EVT_MENU(menu_copy_lb_from_console, wxMaxima::EditMenu)
  EVT_MENU(menu_copy_tex_from_console, wxMaxima::EditMenu)
  EVT_MENU(menu_delete_selection, wxMaxima::EditMenu)
  EVT_MENU(menu_goto_input, wxMaxima::EditMenu)
  EVT_MENU(menu_texform, wxMaxima::MaximaMenu)
  EVT_MENU(menu_to_fact, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_to_gamma, wxMaxima::SimplifyMenu)
  EVT_MENU(menu_goto_output, wxMaxima::EditMenu)
#if WXM_PRINT
  EVT_MENU(wxID_PRINT, wxMaxima::PrintMenu)
 #if defined (__WXMSW__) || (__WXGTK20__)
  EVT_TOOL(tb_print, wxMaxima::PrintMenu)
 #endif
#endif
  EVT_MENU(menu_inc_fontsize, wxMaxima::EditMenu)
  EVT_MENU(menu_dec_fontsize, wxMaxima::EditMenu)
  EVT_MENU(menu_copy_as_bitmap, wxMaxima::EditMenu)
  EVT_MENU(menu_copy_to_file, wxMaxima::EditMenu)
  EVT_MENU(menu_selection_to_input, wxMaxima::EditMenu)
  EVT_MENU(menu_subst, wxMaxima::MaximaMenu)
#if defined (__WXMSW__) || defined (__WXGTK20__)
  EVT_TOOL(tb_open, wxMaxima::FileMenu)
  EVT_TOOL(tb_save, wxMaxima::FileMenu)
  EVT_TOOL(tb_copy, wxMaxima::EditMenu)
  EVT_TOOL(tb_delete, wxMaxima::EditMenu)
  EVT_TOOL(tb_pref, wxMaxima::EditMenu)
  EVT_TOOL(tb_insert_input, wxMaxima::PrependCell)
  EVT_TOOL(tb_insert_text, wxMaxima::PrependCell)
  EVT_TOOL(tb_interrupt, wxMaxima::Interrupt)
  EVT_TOOL(tb_help, wxMaxima::HelpMenu)
#endif
  EVT_SOCKET(socket_server_id, wxMaxima::ServerEvent)
  EVT_SOCKET(socket_client_id, wxMaxima::ClientEvent)
  EVT_UPDATE_UI(menu_copy_from_console, wxMaxima::UpdateMenus)
  EVT_UPDATE_UI(menu_copy_lb_from_console, wxMaxima::UpdateMenus)
  EVT_UPDATE_UI(menu_copy_tex_from_console, wxMaxima::UpdateMenus)
  EVT_UPDATE_UI(menu_inc_fontsize, wxMaxima::UpdateMenus)
  EVT_UPDATE_UI(menu_dec_fontsize, wxMaxima::UpdateMenus)
  EVT_UPDATE_UI(wxID_PRINT, wxMaxima::UpdateMenus)
  EVT_UPDATE_UI(menu_copy_as_bitmap, wxMaxima::UpdateMenus)
  EVT_UPDATE_UI(menu_copy_to_file, wxMaxima::UpdateMenus)
  EVT_UPDATE_UI(menu_edit_input, wxMaxima::UpdateMenus)
  EVT_UPDATE_UI(menu_reeval_input, wxMaxima::UpdateMenus)
#if defined (__WXMSW__) || defined (__WXGTK20__)
  EVT_UPDATE_UI(tb_print, wxMaxima::UpdateToolBar)
  EVT_UPDATE_UI(tb_copy, wxMaxima::UpdateToolBar)
  EVT_UPDATE_UI(tb_delete, wxMaxima::UpdateToolBar)
  EVT_UPDATE_UI(tb_interrupt, wxMaxima::UpdateToolBar)
  EVT_UPDATE_UI(tb_insert_input, wxMaxima::UpdateToolBar)
  EVT_UPDATE_UI(tb_insert_text, wxMaxima::UpdateToolBar)
  EVT_UPDATE_UI(tb_save, wxMaxima::UpdateToolBar)
#endif
  EVT_UPDATE_UI(menu_save_id, wxMaxima::UpdateMenus)
  EVT_UPDATE_UI(menu_add_comment, wxMaxima::UpdateMenus)
  EVT_UPDATE_UI(menu_add_section, wxMaxima::UpdateMenus)
  EVT_UPDATE_UI(menu_add_title, wxMaxima::UpdateMenus)
  EVT_UPDATE_UI(menu_insert_input, wxMaxima::UpdateMenus)
  EVT_UPDATE_UI(menu_selection_to_input, wxMaxima::UpdateMenus)
  EVT_CLOSE(wxMaxima::OnClose)
  EVT_ACTIVATE(wxMaxima::OnActivate)
  EVT_SET_FOCUS(wxMaxima::OnSetFocus)
  EVT_END_PROCESS(maxima_process_id, wxMaxima::OnProcessEvent)
  EVT_MENU(popid_edit, wxMaxima::EditInputMenu)
  EVT_MENU(popid_reeval, wxMaxima::ReEvaluate)
  EVT_MENU(menu_edit_input, wxMaxima::EditInputMenu)
  EVT_MENU(menu_reeval_input, wxMaxima::ReEvaluate)
  EVT_MENU(menu_long_input, wxMaxima::MaximaMenu)
  EVT_MENU(menu_add_comment, wxMaxima::PrependCell)
  EVT_MENU(menu_add_section, wxMaxima::PrependCell)
  EVT_MENU(menu_add_title, wxMaxima::PrependCell)
  EVT_MENU(popid_add_comment, wxMaxima::PrependCell)
  EVT_MENU(menu_insert_input, wxMaxima::PrependCell)
  EVT_MENU(popid_insert_input, wxMaxima::PrependCell)
  EVT_MENU(menu_unfold, wxMaxima::EditMenu)
  EVT_MENU(menu_select_last, wxMaxima::EditMenu)
  EVT_MENU(activate_cell, wxMaxima::HandleCellEvent)
  EVT_MENU(deactivate_cell_cancel, wxMaxima::HandleCellEvent)
  EVT_MENU(deactivate_cell_ok, wxMaxima::HandleCellEvent)
  EVT_MENU(menu_cut, wxMaxima::EditMenu)
  EVT_MENU(menu_paste, wxMaxima::EditMenu)
  EVT_MENU(popid_cut, wxMaxima::PopupMenu)
  EVT_MENU(popid_paste, wxMaxima::PopupMenu)
  EVT_MENU(popid_select_all, wxMaxima::PopupMenu)
END_EVENT_TABLE()
