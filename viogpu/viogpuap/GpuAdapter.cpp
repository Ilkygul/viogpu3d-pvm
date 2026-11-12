/*
 * Copyright (C) 2021-2022 Red Hat, Inc.
 *
 * Written By: Vadim Rozenfeld <vrozenfe@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "pch.h"

GpuAdapter::GpuAdapter(const std::wstring LinkName)
    : m_hThread(NULL), m_hStopEvent(NULL), m_hResolutionEvent(NULL), m_hDC(NULL), m_hAdapter(NULL), m_Index(-1),
      m_PathArrayElements(0), m_ModeInfoArrayElements(0), m_pDisplayPathInfo(NULL), m_pDisplayModeInfo(NULL),
      m_Flag(None)
{
    m_DeviceName = LinkName;
    PrintMessage(L"%ws %ws\n", __FUNCTIONW__, m_DeviceName.c_str());
    Init();
};

void GpuAdapter::UpdateDisplayConfig(void)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    ClearDisplayConfig();

    /* All paths first (the upstream behaviour), active paths if that fails: with a display-less
     * Basic Display adapter next to ours, QDC_ALL_PATHS failed after every boot and the zeroed
     * arrays made every resize land on an inactive path. ERROR_INSUFFICIENT_BUFFER means the
     * topology changed between the two calls: ask for the sizes again. */
    LONG rc = ERROR_GEN_FAILURE;
    const UINT32 filters[] = {QDC_ALL_PATHS, QDC_ONLY_ACTIVE_PATHS};
    for (UINT32 filter : filters)
    {
        for (int attempt = 0; attempt < 3; attempt++)
        {
            ClearDisplayConfig();
            rc = ::GetDisplayConfigBufferSizes(filter, &m_PathArrayElements, &m_ModeInfoArrayElements);
            if (rc != ERROR_SUCCESS)
            {
                break;
            }
            m_pDisplayPathInfo = new DISPLAYCONFIG_PATH_INFO[m_PathArrayElements];
            m_pDisplayModeInfo = new DISPLAYCONFIG_MODE_INFO[m_ModeInfoArrayElements];
            ZeroMemory(m_pDisplayPathInfo, sizeof(DISPLAYCONFIG_PATH_INFO) * m_PathArrayElements);
            ZeroMemory(m_pDisplayModeInfo, sizeof(DISPLAYCONFIG_MODE_INFO) * m_ModeInfoArrayElements);
            rc = ::QueryDisplayConfig(filter, &m_PathArrayElements, m_pDisplayPathInfo, &m_ModeInfoArrayElements,
                                      m_pDisplayModeInfo, NULL);
            if (rc != ERROR_INSUFFICIENT_BUFFER)
            {
                break;
            }
        }
        PrintMessage(L"%ws QueryDisplayConfig(0x%x) rc=%ld paths=%u modes=%u\n", __FUNCTIONW__, filter, rc,
                     m_PathArrayElements, m_ModeInfoArrayElements);
        if (rc == ERROR_SUCCESS)
        {
            break;
        }
    }
    if (rc != ERROR_SUCCESS)
    {
        ClearDisplayConfig();
        return;
    }
    {
        /* QDC_ALL_PATHS also lists the inactive paths, with the same source (GDI) name: take the
         * active path of our source, and the first named one only when none is active. */
        int firstMatch = -1;
        for (UINT PathIdx = 0; PathIdx < GetNumbersOfPathArrayElements(); ++PathIdx)
        {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME SourceName = {};
            SourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            SourceName.header.size = sizeof(SourceName);
            SourceName.header.adapterId = m_pDisplayPathInfo[PathIdx].sourceInfo.adapterId;
            SourceName.header.id = m_pDisplayPathInfo[PathIdx].sourceInfo.id;

            LONG gi = ::DisplayConfigGetDeviceInfo(&SourceName.header);
            PrintMessage(L"%ws path %u src %u gdi=%ws flags=0x%x want=%ws rc=%ld\n", __FUNCTIONW__, PathIdx,
                         m_pDisplayPathInfo[PathIdx].sourceInfo.id, gi == ERROR_SUCCESS ? SourceName.viewGdiDeviceName : L"?",
                         m_pDisplayPathInfo[PathIdx].flags, m_DeviceName.c_str(), gi);
            if (SUCCEEDED(HRESULT_FROM_WIN32(gi)))
            {
                if (wcscmp(m_DeviceName.c_str(), SourceName.viewGdiDeviceName) == 0)
                {
                    if (firstMatch < 0)
                    {
                        firstMatch = (int)PathIdx;
                    }
                    if (m_pDisplayPathInfo[PathIdx].flags & DISPLAYCONFIG_PATH_ACTIVE)
                    {
                        m_Index = PathIdx;
                        firstMatch = -2; /* found an active one */
                        break;
                    }
                }
            }
        }
        if (firstMatch >= 0)
        {
            m_Index = (UINT)firstMatch;
        }
    }
};

DISPLAYCONFIG_MODE_INFO *GpuAdapter::GetDisplayConfig(UINT index)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (index < GetNumbersOfPathArrayElements())
    {
        UINT idx = m_pDisplayPathInfo[index].sourceInfo.modeInfoIdx;
        PrintMessage(L"%ws m_Index %d idx %d active %d\n",
                     __FUNCTIONW__,
                     index,
                     idx,
                     m_pDisplayPathInfo[index].flags & DISPLAYCONFIG_PATH_ACTIVE);
        if (idx < GetNumbersOfModeInfoArrayElements())
        {
            return &m_pDisplayModeInfo[idx];
        }
    }
    return NULL;
}

void GpuAdapter::ClearDisplayConfig(void)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    delete[] m_pDisplayPathInfo;
    delete[] m_pDisplayModeInfo;
    m_pDisplayPathInfo = NULL;
    m_pDisplayModeInfo = NULL;
    m_PathArrayElements = 0;
    m_ModeInfoArrayElements = 0;
}

void GpuAdapter::Init()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    m_hStopEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!m_hStopEvent)
    {
        return;
    }

    m_hDC = ::CreateDC(NULL, m_DeviceName.c_str(), NULL, NULL);

    D3DKMT_OPENADAPTERFROMHDC openAdapter = {0};
    openAdapter.hDc = m_hDC;

    NTSTATUS status = D3DKMTOpenAdapterFromHdc(&openAdapter);
    if (NT_SUCCESS(status))
    {
        UpdateDisplayConfig();
        m_hAdapter = openAdapter.hAdapter;
        if (QueryAdapterId())
        {
            std::wstring EventName = GLOBAL_OBJECTS;
            EventName += RESOLUTION_EVENT_NAME;
            EventName += std::to_wstring(m_Index);
            m_hResolutionEvent = ::OpenEvent(EVENT_ALL_ACCESS | EVENT_MODIFY_STATE, FALSE, EventName.c_str());
            if (m_hResolutionEvent == NULL)
            {
                PrintMessage(L"Cannot open event %ws Error = %d.\n", EventName.c_str(), GetLastError());
                return;
            }
            m_hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ServiceThread, (LPVOID)this, 0, NULL);
            if (m_hThread == NULL)
            {
                PrintMessage(L"Cannot create thread Error = %d.\n", GetLastError());
                return;
            }
        }
    }
    SetStatus(Active);
}

bool GpuAdapter::QueryAdapterId()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (m_hAdapter)
    {
        VIOGPU_ESCAPE data{0};
        data.DataLength = sizeof(ULONG);
        data.Type = VIOGPU_GET_DEVICE_ID;

        D3DKMT_ESCAPE escape = {0};
        escape.hAdapter = m_hAdapter;
        escape.pPrivateDriverData = &data;
        escape.PrivateDriverDataSize = sizeof(data);

        NTSTATUS status = D3DKMTEscape(&escape);
        if (!NT_SUCCESS(status))
        {
            PrintMessage(L"D3DKMTEscape failed with status = 0x%x\n", status);
        }
        else
        {
            m_Index = data.Id;
            return true;
        }
    }
    return false;
}

bool GpuAdapter::GetCurrentResolution(PVIOGPU_DISP_MODE mode)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    DISPLAYCONFIG_MODE_INFO *pConfig = GetDisplayConfig(m_Index);
    if (pConfig)
    {
        mode->XResolution = (USHORT)pConfig->sourceMode.width;
        mode->YResolution = (USHORT)pConfig->sourceMode.height;
        return true;
    }
    return false;
}

bool GpuAdapter::GetCustomResolution(PVIOGPU_DISP_MODE pmode)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (m_hAdapter && pmode)
    {
        VIOGPU_ESCAPE data{0};
        data.DataLength = sizeof(VIOGPU_DISP_MODE);
        data.Type = VIOGPU_GET_CUSTOM_RESOLUTION;

        D3DKMT_ESCAPE escape = {0};
        escape.hAdapter = m_hAdapter;
        escape.pPrivateDriverData = &data;
        escape.PrivateDriverDataSize = sizeof(data);

        NTSTATUS status = D3DKMTEscape(&escape);
        if (NT_SUCCESS(status))
        {
            pmode->XResolution = data.Resolution.XResolution;
            pmode->YResolution = data.Resolution.YResolution;
            PrintMessage(L"%ws (%dx%d)\n", __FUNCTIONW__, pmode->XResolution, pmode->YResolution);
            return true;
        }
        PrintMessage(L"D3DKMTEscape failed with status = 0x%0X\n", status);
    }
    return false;
}

bool GpuAdapter::SetResolution(PVIOGPU_DISP_MODE mode)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (m_pDisplayPathInfo == NULL || m_pDisplayModeInfo == NULL || m_Index >= GetNumbersOfPathArrayElements())
    {
        PrintMessage(L"%ws no display path\n", __FUNCTIONW__);
        return false;
    }
    // An inactive path carries DISPLAYCONFIG_PATH_MODE_IDX_INVALID, which would
    // index the mode info array out of bounds.
    UINT ix = m_pDisplayPathInfo[m_Index].sourceInfo.modeInfoIdx;
    if (ix >= GetNumbersOfModeInfoArrayElements())
    {
        PrintMessage(L"%ws m_Index %d has no mode info (idx %d)\n", __FUNCTIONW__, m_Index, ix);
        return false;
    }
    PrintMessage(L"%ws m_Index %d %d (%dx%d)\n", __FUNCTIONW__, m_Index, ix, mode->XResolution, mode->YResolution);
    m_pDisplayModeInfo[ix].sourceMode.width = mode->XResolution;
    m_pDisplayModeInfo[ix].sourceMode.height = mode->YResolution;
    LONG rc = SetDisplayConfig(m_PathArrayElements,
                               m_pDisplayPathInfo,
                               m_ModeInfoArrayElements,
                               m_pDisplayModeInfo,
                               SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES | SDC_SAVE_TO_DATABASE);
    PrintMessage(L"%ws SetDisplayConfig rc=%ld\n", __FUNCTIONW__, rc);
    return rc == ERROR_SUCCESS;
}

bool GpuAdapter::SyncResolution(void)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    VIOGPU_DISP_MODE custom = {0};
    UpdateDisplayConfig();
    if (GetCustomResolution(&custom))
    {
        VIOGPU_DISP_MODE current = {0};
        // The resolution event fires for every host display event, not only for
        // actual mode changes.  SetDisplayConfig with SDC_APPLY forces a modeset
        // (screen blank + DWM swapchain rebuild) even when the supplied mode equals
        // the current one, so skip no-op applies.
        if (GetCurrentResolution(&current) && current.XResolution == custom.XResolution &&
            current.YResolution == custom.YResolution)
        {
            PrintMessage(L"%ws resolution unchanged (%dx%d), skipping apply\n", __FUNCTIONW__, custom.XResolution,
                         custom.YResolution);
            return false;
        }
        PrintMessage(L"%ws host %dx%d, current %dx%d: applying\n", __FUNCTIONW__, custom.XResolution,
                     custom.YResolution, current.XResolution, current.YResolution);
        if (!SetResolution(&custom))
        {
            return false;
        }
        UpdateDisplayConfig(); /* paths and modes after the mode change */
        /* A display-only driver gets dirty rects only; after a mode change parts of the new
         * framebuffer would keep old content until something redraws them. Ask everything to repaint,
         * once right away and once after Explorer has relaid out. */
        for (DWORD delay : {700, 2500})
        {
            Sleep(delay);
            RedrawWindow(NULL, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
        return true;
    }
    return false;
}

// The KMD pulses the resolution event, so a host resize that arrives while an
// earlier one is still being applied (the repaint sleeps) would be missed.
// After every apply look again: a second pass finds the size unchanged and
// stops, a size that moved in the meantime is applied too.
void GpuAdapter::SyncAndConfirm(void)
{
    for (int pass = 0; pass < 4 && SyncResolution(); pass++)
    {
    }
}

void GpuAdapter::Run()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (m_hThread != NULL && m_hStopEvent != NULL && m_hResolutionEvent != NULL)
    {
        SyncAndConfirm();

        const HANDLE handles[] = {m_hStopEvent, m_hResolutionEvent};
        while (1)
        {
            if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0)
            {
                break;
            }
            SyncAndConfirm();
        }
    }
}

DWORD WINAPI GpuAdapter::ServiceThread(GpuAdapter *ptr)
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    ptr->Run();
    return 0;
}

void GpuAdapter::Close()
{
    PrintMessage(L"%ws\n", __FUNCTIONW__);

    if (m_hThread != NULL)
    {
        if (m_hStopEvent != NULL)
        {
            SetEvent(m_hStopEvent);
            if (WAIT_TIMEOUT == WaitForSingleObject(m_hThread, 1000))
            {
                PrintMessage(L"Cannot close thread after 1 sec\n");
                TerminateThread(m_hThread, 0);
            }
        }
        m_hThread = NULL;
    }

    if (m_hStopEvent)
    {
        CloseHandle(m_hStopEvent);
        m_hStopEvent = NULL;
    }

    if (m_hResolutionEvent)
    {
        CloseHandle(m_hResolutionEvent);
        m_hResolutionEvent = NULL;
    }

    if (m_hAdapter)
    {
        D3DKMT_CLOSEADAPTER close = {m_hAdapter};
        NTSTATUS status = D3DKMTCloseAdapter(&close);
        if (!NT_SUCCESS(status))
        {
            PrintMessage(L"D3DKMTCloseAdapter failed with status = 0x%x.\n", status);
        }
        m_hAdapter = NULL;
    }

    if (m_hDC != NULL)
    {
        ReleaseDC(NULL, m_hDC);
        m_hDC = NULL;
    }
}
