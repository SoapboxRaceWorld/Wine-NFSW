/*
 * Joystick testing control panel applet
 *
 * Copyright 2012 Lucas Fialho Zawacki
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */

#define NONAMELESSUNION
#define COBJMACROS
#define CONST_VTABLE

#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <commctrl.h>
#include <cpl.h>
#include "ole2.h"

#include "wine/debug.h"
#include "joy.h"

WINE_DEFAULT_DEBUG_CHANNEL(joycpl);

DECLSPEC_HIDDEN HMODULE hcpl;

/*********************************************************************
 *  DllMain
 */
BOOL WINAPI DllMain(HINSTANCE hdll, DWORD reason, LPVOID reserved)
{
    TRACE("(%p, %d, %p)\n", hdll, reason, reserved);

    switch (reason)
    {
        case DLL_WINE_PREATTACH:
            return FALSE;  /* prefer native version */

        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hdll);
            hcpl = hdll;
    }
    return TRUE;
}

/***********************************************************************
 *  enum_callback [internal]
 *   Enumerates, creates and sets the common data format for all the joystick devices.
 *   First time it checks if space for the joysticks was already reserved
 *   and if not, just counts how many there are.
 */
static BOOL CALLBACK enum_callback(const DIDEVICEINSTANCEW *instance, void *context)
{
    struct JoystickData *data = context;
    struct Joystick *joystick;
    DIDEVCAPS caps;

    if (data->joysticks == NULL)
    {
        data->num_joysticks += 1;
        return DIENUM_CONTINUE;
    }

    joystick = &data->joysticks[data->cur_joystick];
    data->cur_joystick += 1;

    IDirectInput8_CreateDevice(data->di, &instance->guidInstance, &joystick->device, NULL);
    IDirectInputDevice8_SetDataFormat(joystick->device, &c_dfDIJoystick);

    joystick->instance = *instance;

    caps.dwSize = sizeof(caps);
    IDirectInputDevice8_GetCapabilities(joystick->device, &caps);

    joystick->num_buttons = caps.dwButtons;
    joystick->num_axes = caps.dwAxes;

    return DIENUM_CONTINUE;
}

/***********************************************************************
 *  initialize_joysticks [internal]
 */
static void initialize_joysticks(struct JoystickData *data)
{
    data->num_joysticks = 0;
    data->cur_joystick = 0;
    IDirectInput8_EnumDevices(data->di, DI8DEVCLASS_GAMECTRL, enum_callback, data, DIEDFL_ATTACHEDONLY);
    data->joysticks = HeapAlloc(GetProcessHeap(), 0, sizeof(struct Joystick) * data->num_joysticks);

    /* Get all the joysticks */
    IDirectInput8_EnumDevices(data->di, DI8DEVCLASS_GAMECTRL, enum_callback, data, DIEDFL_ATTACHEDONLY);
}

/***********************************************************************
 *  destroy_joysticks [internal]
 */
static void destroy_joysticks(struct JoystickData *data)
{
    int i;

    for (i = 0; i < data->num_joysticks; i++)
    {
        IDirectInputDevice8_Unacquire(data->joysticks[i].device);
        IDirectInputDevice8_Release(data->joysticks[i].device);
    }

    HeapFree(GetProcessHeap(), 0, data->joysticks);
}

/*********************************************************************
 * list_dlgproc [internal]
 *
 */
static INT_PTR CALLBACK list_dlgproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    TRACE("(%p, 0x%08x/%d, 0x%lx)\n", hwnd, msg, msg, lparam);
    switch (msg)
    {
        case WM_INITDIALOG:
        {
            int i;
            struct JoystickData *data = (struct JoystickData*) ((PROPSHEETPAGEW*)lparam)->lParam;

            /* Set dialog information */
            for (i = 0; i < data->num_joysticks; i++)
            {
                struct Joystick *joy = &data->joysticks[i];
                SendDlgItemMessageW(hwnd, IDC_JOYSTICKLIST, LB_ADDSTRING, 0, (LPARAM) joy->instance.tszInstanceName);
            }

            return TRUE;
        }

        case WM_COMMAND:

            switch (LOWORD(wparam))
            {
                case IDC_BUTTONDISABLE:
                    FIXME("Disable selected joystick from being enumerated\n");
                    break;

                case IDC_BUTTONENABLE:
                    FIXME("Re-Enable selected joystick\n");
                    break;
            }

            return TRUE;

        case WM_NOTIFY:
            return TRUE;

        default:
            break;
    }
    return FALSE;
}

/*********************************************************************
 * Joystick testing functions
 *
 */
static void dump_joy_state(DIJOYSTATE* st, int num_buttons)
{
    int i;
    TRACE("Ax (% 5d,% 5d,% 5d)\n", st->lX,st->lY, st->lZ);
    TRACE("RAx (% 5d,% 5d,% 5d)\n", st->lRx, st->lRy, st->lRz);
    TRACE("Slider (% 5d,% 5d)\n", st->rglSlider[0], st->rglSlider[1]);
    TRACE("Pov (% 5d,% 5d,% 5d,% 5d)\n", st->rgdwPOV[0], st->rgdwPOV[1], st->rgdwPOV[2], st->rgdwPOV[3]);

    TRACE("Buttons ");
    for(i=0; i < num_buttons; i++)
        TRACE("  %c",st->rgbButtons[i] ? 'x' : 'o');
    TRACE("\n");
}

static void poll_input(const struct Joystick *joy, DIJOYSTATE *state)
{
    HRESULT  hr;

    hr = IDirectInputDevice8_Poll(joy->device);

    /* If it failed, try to acquire the joystick */
    if (FAILED(hr))
    {
        hr = IDirectInputDevice8_Acquire(joy->device);
        while (hr == DIERR_INPUTLOST) hr = IDirectInputDevice8_Acquire(joy->device);
    }

    if (hr == DIERR_OTHERAPPHASPRIO) return;

    IDirectInputDevice8_GetDeviceState(joy->device, sizeof(DIJOYSTATE), state);
}

static DWORD WINAPI input_thread(void *param)
{
    int axes_pos[TEST_MAX_AXES][2];
    DIJOYSTATE state;
    struct JoystickData *data = param;

    /* Setup POV as clock positions
     *         0
     *   31500    4500
     * 27000  -1    9000
     *   22500   13500
     *       18000
     */
    int ma = TEST_AXIS_MAX;
    int pov_val[9] = {0, 4500, 9000, 13500,
                      18000, 22500, 27000, 31500, -1};
    int pov_pos[9][2] = { {0, -ma}, {ma/2, -ma/2}, {ma, 0}, {ma/2, ma/2},
                          {0, ma}, {-ma/2, ma/2}, {-ma, 0}, {-ma/2, -ma/2}, {0, 0} };

    ZeroMemory(&state, sizeof(state));

    while (!data->stop)
    {
        int i;
        poll_input(&data->joysticks[data->chosen_joystick], &state);

        dump_joy_state(&state, data->joysticks[data->chosen_joystick].num_buttons);

        /* Indicate pressed buttons */
        for (i = 0; i < data->joysticks[data->chosen_joystick].num_buttons; i++)
            if (state.rgbButtons[i])
                SendMessageW(data->buttons[i], BM_SETSTATE, TRUE, 0);

        /* Indicate axis positions, axes showing are hardcoded for now */
        axes_pos[0][0] = state.lX;
        axes_pos[0][1] = state.lY;
        axes_pos[1][0] = state.lRx;
        axes_pos[1][1] = state.lRy;
        axes_pos[2][0] = state.lZ;
        axes_pos[2][1] = state.lRz;

        /* Set pov values */
        for (i = 0; i < sizeof(pov_val)/sizeof(pov_val[0]); i++)
        {
            if (state.rgdwPOV[0] == pov_val[i])
            {
                axes_pos[3][0] = pov_pos[i][0];
                axes_pos[3][1] = pov_pos[i][1];
            }
        }

        for (i = 0; i < TEST_MAX_AXES; i++)
            SetWindowPos(data->axes[i], 0,
                        TEST_AXIS_X + TEST_NEXT_AXIS_X*i + axes_pos[i][0],
                        TEST_AXIS_Y + axes_pos[i][1],
                        0, 0, SWP_NOZORDER | SWP_NOSIZE);

        Sleep(TEST_POLL_TIME);

        /* Reset button state */
        for (i = 0; i < data->joysticks[data->chosen_joystick].num_buttons; i++)
            SendMessageW(data->buttons[i], BM_SETSTATE, FALSE, 0);
    }

    return 0;
}

static void test_handle_joychange(HWND hwnd, struct JoystickData *data)
{
    int i;

    if (data->num_joysticks == 0) return;

    data->chosen_joystick = SendDlgItemMessageW(hwnd, IDC_TESTSELECTCOMBO, CB_GETCURSEL, 0, 0);

    /* Enable only  buttons present in the device */
    for (i = 0; i < TEST_MAX_BUTTONS; i++)
        ShowWindow(data->buttons[i], i <= data->joysticks[data->chosen_joystick].num_buttons);
}

/*********************************************************************
 * button_number_to_wchar [internal]
 *  Transforms an integer in the interval [0,99] into a 2 character WCHAR string
 */
static void button_number_to_wchar(int n, WCHAR str[3])
{
    str[1] = n % 10 + '0';
    n /= 10;
    str[0] = n % 10 + '0';
    str[2] = '\0';
}

static void draw_joystick_buttons(HWND hwnd, struct JoystickData* data)
{
    int i;
    int row = 0, col = 0;
    WCHAR button_label[3];
    HINSTANCE hinst = (HINSTANCE) GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    static WCHAR button_class[] = {'B','u','t','t','o','n','\0'};

    for (i = 0; i < TEST_MAX_BUTTONS; i++)
    {
        if ((i % TEST_BUTTON_COL_MAX) == 0 && i != 0)
        {
            row += 1;
            col = 0;
        }

        button_number_to_wchar(i + 1, button_label);

        data->buttons[i] = CreateWindowW(button_class, button_label, WS_CHILD,
            TEST_BUTTON_X + TEST_NEXT_BUTTON_X*col, TEST_BUTTON_Y + TEST_NEXT_BUTTON_Y*row,
            TEST_BUTTON_SIZE_X, TEST_BUTTON_SIZE_Y,
            hwnd, NULL, NULL, hinst);

        col += 1;
    }
}

static void draw_joystick_axes(HWND hwnd, struct JoystickData* data)
{
    int i;
    struct Joystick *joy;
    DIPROPRANGE propRange;
    HINSTANCE hinst = (HINSTANCE) GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    static const WCHAR button_class[] = {'B','u','t','t','o','n','\0'};
    static const WCHAR axes_names[TEST_MAX_AXES][7] = { {'X',',','Y','\0'}, {'R','x',',','R','y','\0'},
                                                        {'Z',',','R','z','\0'}, {'P','O','V','\0'} };
    static const DWORD axes_idc[TEST_MAX_AXES] = { IDC_TESTGROUPXY, IDC_TESTGROUPRXRY,
                                                   IDC_TESTGROUPZRZ, IDC_TESTGROUPPOV };

    /* Set axis range to ease the GUI visualization */
    for (i = 0; i < data->num_joysticks; i++)
    {
        joy = &data->joysticks[i];
        propRange.diph.dwSize = sizeof(DIPROPRANGE);
        propRange.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        propRange.diph.dwHow = DIPH_DEVICE;
        propRange.diph.dwObj = 0;
        propRange.lMin = TEST_AXIS_MIN;
        propRange.lMax = TEST_AXIS_MAX;

        IDirectInputDevice_SetProperty(joy->device, DIPROP_RANGE, &propRange.diph);
    }

    for (i = 0; i < TEST_MAX_AXES; i++)
    {
        /* Set axis box name */
        SetWindowTextW(GetDlgItem(hwnd, axes_idc[i]), axes_names[i]);

        data->axes[i] = CreateWindowW( button_class, NULL, WS_CHILD | WS_VISIBLE,
            TEST_AXIS_X + TEST_NEXT_AXIS_X*i, TEST_AXIS_Y,
            TEST_AXIS_SIZE_X, TEST_AXIS_SIZE_Y,
            hwnd, (HMENU) IDC_JOYSTICKAXES + i, NULL, hinst);
    }
}

/*********************************************************************
 * test_dlgproc [internal]
 *
 */
static INT_PTR CALLBACK test_dlgproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    static HANDLE thread;
    static struct JoystickData *data;
    TRACE("(%p, 0x%08x/%d, 0x%lx)\n", hwnd, msg, msg, lparam);

    switch (msg)
    {
        case WM_INITDIALOG:
        {
            int i;

            data = (struct JoystickData*) ((PROPSHEETPAGEW*)lparam)->lParam;

            /* Add enumerated joysticks to the combobox */
            for (i = 0; i < data->num_joysticks; i++)
            {
                struct Joystick *joy = &data->joysticks[i];
                SendDlgItemMessageW(hwnd, IDC_TESTSELECTCOMBO, CB_ADDSTRING, 0, (LPARAM) joy->instance.tszInstanceName);
            }

            draw_joystick_buttons(hwnd, data);
            draw_joystick_axes(hwnd, data);

            return TRUE;
        }

        case WM_COMMAND:
            switch(wparam)
            {
                case MAKEWPARAM(IDC_TESTSELECTCOMBO, CBN_SELCHANGE):
                    test_handle_joychange(hwnd, data);
                break;
            }
            return TRUE;

        case WM_NOTIFY:
            switch(((LPNMHDR)lparam)->code)
            {
                case PSN_SETACTIVE:
                {
                    DWORD tid;

                    /* Initialize input thread */
                    if (data->num_joysticks > 0)
                    {
                        data->stop = FALSE;

                        /* Set the first joystick as default */
                        SendDlgItemMessageW(hwnd, IDC_TESTSELECTCOMBO, CB_SETCURSEL, 0, 0);
                        test_handle_joychange(hwnd, data);

                        thread = CreateThread(NULL, 0, input_thread, (void*) data, 0, &tid);
                    }
                }
                break;

                case PSN_RESET:
                    /* Stop input thread */
                    data->stop = TRUE;
                    CloseHandle(thread);
                break;
            }
            return TRUE;
    }
    return FALSE;
}

/******************************************************************************
 * propsheet_callback [internal]
 */
static int CALLBACK propsheet_callback(HWND hwnd, UINT msg, LPARAM lparam)
{
    TRACE("(%p, 0x%08x/%d, 0x%lx)\n", hwnd, msg, msg, lparam);
    switch (msg)
    {
        case PSCB_INITIALIZED:
            break;
    }
    return 0;
}

/******************************************************************************
 * display_cpl_sheets [internal]
 *
 * Build and display the dialog with all control panel propertysheets
 *
 */
static void display_cpl_sheets(HWND parent, struct JoystickData *data)
{
    INITCOMMONCONTROLSEX icex;
    PROPSHEETPAGEW psp[NUM_PROPERTY_PAGES];
    PROPSHEETHEADERW psh;
    DWORD id = 0;

    OleInitialize(NULL);
    /* Initialize common controls */
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    ZeroMemory(&psh, sizeof(psh));
    ZeroMemory(psp, sizeof(psp));

    /* Fill out all PROPSHEETPAGE */
    psp[id].dwSize = sizeof (PROPSHEETPAGEW);
    psp[id].hInstance = hcpl;
    psp[id].u.pszTemplate = MAKEINTRESOURCEW(IDD_LIST);
    psp[id].pfnDlgProc = list_dlgproc;
    psp[id].lParam = (INT_PTR) data;
    id++;

    psp[id].dwSize = sizeof (PROPSHEETPAGEW);
    psp[id].hInstance = hcpl;
    psp[id].u.pszTemplate = MAKEINTRESOURCEW(IDD_TEST);
    psp[id].pfnDlgProc = test_dlgproc;
    psp[id].lParam = (INT_PTR) data;
    id++;

    psp[id].dwSize = sizeof (PROPSHEETPAGEW);
    psp[id].hInstance = hcpl;
    psp[id].u.pszTemplate = MAKEINTRESOURCEW(IDD_FORCEFEEDBACK);
    psp[id].pfnDlgProc = NULL;
    psp[id].lParam = (INT_PTR) data;
    id++;

    /* Fill out the PROPSHEETHEADER */
    psh.dwSize = sizeof (PROPSHEETHEADERW);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_USEICONID | PSH_USECALLBACK;
    psh.hwndParent = parent;
    psh.hInstance = hcpl;
    psh.pszCaption = MAKEINTRESOURCEW(IDS_CPL_NAME);
    psh.nPages = id;
    psh.u3.ppsp = psp;
    psh.pfnCallback = propsheet_callback;

    /* display the dialog */
    PropertySheetW(&psh);

    OleUninitialize();
}

/*********************************************************************
 * CPlApplet (joy.cpl.@)
 *
 * Control Panel entry point
 *
 * PARAMS
 *  hWnd    [I] Handle for the Control Panel Window
 *  command [I] CPL_* Command
 *  lParam1 [I] first extra Parameter
 *  lParam2 [I] second extra Parameter
 *
 * RETURNS
 *  Depends on the command
 *
 */
LONG CALLBACK CPlApplet(HWND hwnd, UINT command, LPARAM lParam1, LPARAM lParam2)
{
    static struct JoystickData data;
    TRACE("(%p, %u, 0x%lx, 0x%lx)\n", hwnd, command, lParam1, lParam2);

    switch (command)
    {
        case CPL_INIT:
        {
            HRESULT hr;

            /* Initialize dinput */
            hr = DirectInput8Create(GetModuleHandleW(NULL), DIRECTINPUT_VERSION, &IID_IDirectInput8W, (void**)&data.di, NULL);

            if (FAILED(hr))
            {
                ERR("Failed to initialize DirectInput: 0x%08x\n", hr);
                return FALSE;
            }

            /* Then get all the connected joysticks */
            initialize_joysticks(&data);

            return TRUE;
        }
        case CPL_GETCOUNT:
            return 1;

        case CPL_INQUIRE:
        {
            CPLINFO *appletInfo = (CPLINFO *) lParam2;

            appletInfo->idName = IDS_CPL_NAME;
            appletInfo->idInfo = IDS_CPL_INFO;
            appletInfo->lData = 0;
            return TRUE;
        }

        case CPL_DBLCLK:
            display_cpl_sheets(hwnd, &data);
            break;

        case CPL_STOP:
            destroy_joysticks(&data);

            /* And destroy dinput too */
            IDirectInput8_Release(data.di);
            break;
    }

    return FALSE;
}
