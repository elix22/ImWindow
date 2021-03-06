#include "ImwPlatformWindowBGFX.h"

#include "ImwWindowManager.h"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/bx.h>
#include <bx/fpumath.h>
#include <bgfx/embedded_shader.h>

#include "imgui/vs_ocornut_imgui.bin.h"
#include "imgui/fs_ocornut_imgui.bin.h"

static const bgfx::EmbeddedShader s_embeddedShaders[] =
{
	BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
	BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),

	BGFX_EMBEDDED_SHADER_END()
};

using namespace ImWindow;

ImwPlatformWindowBGFX::ImwPlatformWindowBGFX(EPlatformWindowType eType, bool bCreateState, bgfx::RendererType::Enum eRenderer)
	: ImwPlatformWindow(eType, bCreateState)
{
	m_eRenderer = eRenderer;
}

ImwPlatformWindowBGFX::~ImwPlatformWindowBGFX()
{
	if (m_eType == E_PLATFORM_WINDOW_TYPE_MAIN)
	{
		bgfx::destroyProgram(m_hProgram);
		bgfx::destroyTexture(m_hTexture);
		bgfx::destroyUniform(m_hUniformTexture);
	}

	bgfx::destroyDynamicIndexBuffer(m_hIndexBuffer);
	bgfx::destroyDynamicVertexBuffer(m_hVertexBuffer);

	bgfx::destroyFrameBuffer(m_hFrameBufferHandle);
	bgfx::frame();
	bgfx::frame();
	ImwSafeDelete(m_pWindow);
}

inline bool checkAvailTransientBuffers(uint32_t _numVertices, const bgfx::VertexDecl& _decl, uint32_t _numIndices)
{
	return _numVertices == bgfx::getAvailTransientVertexBuffer(_numVertices, _decl)
		&& _numIndices == bgfx::getAvailTransientIndexBuffer(_numIndices)
		;
}

bool ImwPlatformWindowBGFX::Init(ImwPlatformWindow* pMain)
{
	ImwPlatformWindowBGFX* pMainBGFX = ((ImwPlatformWindowBGFX*)pMain);

	EasyWindow::EWindowStyle eStyle = EasyWindow::E_STYLE_NORMAL;
	if (m_eType == E_PLATFORM_WINDOW_TYPE_DRAG_PREVIEW)
		eStyle = EasyWindow::E_STYLE_POPUP;

	m_pWindow = EasyWindow::Create("ImwPlatformWindowBGFX", 800, 600, false, pMain != NULL ? pMainBGFX->m_pWindow : NULL, eStyle, EasyWindow::E_FLAG_OWN_DC | EasyWindow::E_FLAG_ACCEPT_FILES_DROP);
	m_pWindow->OnClose.Set(this, &ImwPlatformWindowBGFX::OnClose);
	m_pWindow->OnFocus.Set(this, &ImwPlatformWindowBGFX::OnFocus);
	m_pWindow->OnSize.Set(this, &ImwPlatformWindowBGFX::OnSize);
	m_pWindow->OnMouseButton.Set(this, &ImwPlatformWindowBGFX::OnMouseButton);
	m_pWindow->OnMouseMove.Set(this, &ImwPlatformWindowBGFX::OnMouseMove);
	m_pWindow->OnMouseWheel.Set(this, &ImwPlatformWindowBGFX::OnMouseWheel);
	m_pWindow->OnKey.Set(this, &ImwPlatformWindowBGFX::OnKey);
	m_pWindow->OnChar.Set(this, &ImwPlatformWindowBGFX::OnChar);
	m_pWindow->OnDropFiles.Set(this, &ImwPlatformWindowBGFX::OnDropFiles);

	if (m_eType == E_PLATFORM_WINDOW_TYPE_MAIN)
	{
		bgfx::PlatformData pd;
		memset(&pd, 0, sizeof(pd));
		pd.nwh = m_pWindow->GetHandle();
		bgfx::setPlatformData(pd);

		bgfx::init(m_eRenderer);
		bgfx::reset(m_pWindow->GetClientWidth(), m_pWindow->GetClientHeight());
	}

	if (m_eType == E_PLATFORM_WINDOW_TYPE_DRAG_PREVIEW)
		m_pWindow->SetAlpha(128);

	m_hFrameBufferHandle = bgfx::createFrameBuffer(m_pWindow->GetHandle(), uint16_t(m_pWindow->GetClientWidth()), uint16_t(m_pWindow->GetClientHeight()));

	bgfx::setViewFrameBuffer(255, m_hFrameBufferHandle);

	SetContext(false);
	ImGuiIO& io = ImGui::GetIO();
	
	if (pMainBGFX != NULL)
	{
		m_hTexture = pMainBGFX->m_hTexture;
		m_hUniformTexture = pMainBGFX->m_hUniformTexture;
		m_hProgram = pMainBGFX->m_hProgram;
		m_oVertexDecl = pMainBGFX->m_oVertexDecl;
	}
	else
	{
		uint8_t* data;
		int32_t width;
		int32_t height;
		io.Fonts->AddFontDefault();
		io.Fonts->GetTexDataAsRGBA32(&data, &width, &height);

		m_hTexture = bgfx::createTexture2D(
			(uint16_t)width
			, (uint16_t)height
			, false
			, 1
			, bgfx::TextureFormat::BGRA8
			, 0
			, bgfx::copy(data, width*height * 4)
		);

		m_hUniformTexture = bgfx::createUniform("s_tex", bgfx::UniformType::Int1);

		m_hProgram = bgfx::createProgram(
			bgfx::createEmbeddedShader(s_embeddedShaders, m_eRenderer, "vs_ocornut_imgui")
			, bgfx::createEmbeddedShader(s_embeddedShaders, m_eRenderer, "fs_ocornut_imgui")
			, true
		);

		m_oVertexDecl
			.begin()
			.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
			.end();
	}
	uint16_t iFlag = 0;
	if (sizeof(ImDrawIdx) == 4)
		iFlag |= BGFX_BUFFER_INDEX32;
	m_hVertexBuffer = bgfx::createDynamicVertexBuffer((uint32_t)1, m_oVertexDecl, BGFX_BUFFER_ALLOW_RESIZE);
	m_hIndexBuffer = bgfx::createDynamicIndexBuffer((uint32_t)1, BGFX_BUFFER_ALLOW_RESIZE| iFlag);

	io.KeyMap[ImGuiKey_Tab] = EasyWindow::KEY_TAB;                       // Keyboard mapping. ImGui will use those indices to peek into the io.KeyDown[] array that we will update during the application lifetime.
	io.KeyMap[ImGuiKey_LeftArrow] = EasyWindow::KEY_LEFT;
	io.KeyMap[ImGuiKey_RightArrow] = EasyWindow::KEY_RIGHT;
	io.KeyMap[ImGuiKey_UpArrow] = EasyWindow::KEY_UP;
	io.KeyMap[ImGuiKey_DownArrow] = EasyWindow::KEY_DOWN;
	io.KeyMap[ImGuiKey_PageUp] = EasyWindow::KEY_PAGEUP;
	io.KeyMap[ImGuiKey_PageDown] = EasyWindow::KEY_PAGEDOWN;
	io.KeyMap[ImGuiKey_Home] = EasyWindow::KEY_HOME;
	io.KeyMap[ImGuiKey_End] = EasyWindow::KEY_END;
	io.KeyMap[ImGuiKey_Delete] = EasyWindow::KEY_DELETE;
	io.KeyMap[ImGuiKey_Backspace] = EasyWindow::KEY_BACKSPACE;
	io.KeyMap[ImGuiKey_Enter] = EasyWindow::KEY_RETURN;
	io.KeyMap[ImGuiKey_Escape] = EasyWindow::KEY_ESC;
	io.KeyMap[ImGuiKey_A] = EasyWindow::KEY_A;
	io.KeyMap[ImGuiKey_C] = EasyWindow::KEY_C;
	io.KeyMap[ImGuiKey_V] = EasyWindow::KEY_V;
	io.KeyMap[ImGuiKey_X] = EasyWindow::KEY_X;
	io.KeyMap[ImGuiKey_Y] = EasyWindow::KEY_Y;
	io.KeyMap[ImGuiKey_Z] = EasyWindow::KEY_Z;

	io.ImeWindowHandle = m_pWindow->GetHandle();

	RestoreContext(false);

	//m_hCursorArrow = LoadCursor( NULL, IDC_ARROW );
	//m_hCursorResizeNS = LoadCursor( NULL, IDC_SIZENS );
	//m_hCursorResizeWE = LoadCursor( NULL, IDC_SIZEWE );

	if( pMain == NULL )
	{
		//ImGui_ImplDX11_NewFrame();
	}

	return true;
}

ImVec2 ImwPlatformWindowBGFX::GetPosition() const
{
	return ImVec2(float(m_pWindow->GetClientPositionX()), float(m_pWindow->GetClientPositionY()));
}

ImVec2 ImwPlatformWindowBGFX::GetSize() const
{
	return ImVec2(float(m_pWindow->GetClientWidth()), float(m_pWindow->GetClientHeight()));
}

bool ImwPlatformWindowBGFX::IsWindowMaximized() const
{
	return m_pWindow->IsMaximized();
}

bool ImwPlatformWindowBGFX::IsWindowMinimized() const
{
	return m_pWindow->IsMinimized();
}

void ImwPlatformWindowBGFX::Show(bool bShow)
{
	m_pWindow->Show(bShow);
}

void ImwPlatformWindowBGFX::SetSize(int iWidth, int iHeight)
{
	m_pWindow->SetSize(iWidth, iHeight, true);
}

void ImwPlatformWindowBGFX::SetPosition(int iX, int iY)
{
	m_pWindow->SetPosition(iX, iY, true);
}

void ImwPlatformWindowBGFX::SetWindowMaximized(bool bMaximized)
{
	m_pWindow->SetMaximized(bMaximized);
}

void ImwPlatformWindowBGFX::SetWindowMinimized(bool bMinimized)
{
	m_pWindow->SetMinimized(bMinimized);
}

void ImwPlatformWindowBGFX::SetTitle(const ImwChar* pTitle)
{
	m_pWindow->SetTitle(pTitle);
}

void ImwPlatformWindowBGFX::PreUpdate()
{
	m_pWindow->Update();
	ImGuiIO& oIO = m_pContext->IO;
	oIO.KeyCtrl = m_pWindow->IsKeyCtrlDown();
	oIO.KeyShift = m_pWindow->IsKeyShiftDown();
	oIO.KeyAlt = m_pWindow->IsKeyAltDown();
	oIO.KeySuper = false;

	if (oIO.MouseDrawCursor)
	{
		m_pWindow->SetCursor(EasyWindow::E_CURSOR_NONE);
	}
	else if (oIO.MousePos.x != -1.f && oIO.MousePos.y != -1.f)
	{
		switch (m_pContext->MouseCursor)
		{
		case ImGuiMouseCursor_Arrow:
			m_pWindow->SetCursor(EasyWindow::E_CURSOR_ARROW);
			break;
		case ImGuiMouseCursor_TextInput:         // When hovering over InputText, etc.
			m_pWindow->SetCursor(EasyWindow::E_CURSOR_TEXT_INPUT);
			break;
		case ImGuiMouseCursor_Move:              // Unused
			m_pWindow->SetCursor(EasyWindow::E_CURSOR_HAND);
			break;
		case ImGuiMouseCursor_ResizeNS:          // Unused
			m_pWindow->SetCursor(EasyWindow::E_CURSOR_RESIZE_NS);
			break;
		case ImGuiMouseCursor_ResizeEW:          // When hovering over a column
			m_pWindow->SetCursor(EasyWindow::E_CURSOR_RESIZE_EW);
			break;
		case ImGuiMouseCursor_ResizeNESW:        // Unused
			m_pWindow->SetCursor(EasyWindow::E_CURSOR_RESIZE_NESW);
			break;
		case ImGuiMouseCursor_ResizeNWSE:        // When hovering over the bottom-right corner of a window
			m_pWindow->SetCursor(EasyWindow::E_CURSOR_RESIZE_NWSE);
			break;
		}
	}
}

bool ImwPlatformWindowBGFX::OnClose()
{
	ImwPlatformWindow::OnClose();
	return true;
}

void ImwPlatformWindowBGFX::OnFocus(bool bHasFocus)
{
	if (!bHasFocus)
		OnLoseFocus();
}

void ImwPlatformWindowBGFX::OnSize(int iWidth, int iHeight)
{
	bgfx::frame();
	if (bgfx::isValid(m_hFrameBufferHandle))
		bgfx::destroyFrameBuffer(m_hFrameBufferHandle);
	m_hFrameBufferHandle = bgfx::createFrameBuffer(m_pWindow->GetHandle(), uint16_t(m_pWindow->GetClientWidth()), uint16_t(m_pWindow->GetClientHeight()));
	
	if (m_eType == E_PLATFORM_WINDOW_TYPE_MAIN)
	{
		bgfx::setViewFrameBuffer(255, m_hFrameBufferHandle);
		bgfx::reset(iWidth, iHeight);
	}
}

void ImwPlatformWindowBGFX::OnMouseButton(int iButton, bool bDown)
{
	m_pContext->IO.MouseDown[iButton] = bDown;
}

void ImwPlatformWindowBGFX::OnMouseMove(int iX, int iY)
{
	m_pContext->IO.MousePos = ImVec2((float)iX, (float)iY);
}

void ImwPlatformWindowBGFX::OnMouseWheel( int iStep )
{
	m_pContext->IO.MouseWheel += iStep;
}

void ImwPlatformWindowBGFX::OnKey(EasyWindow::EKey eKey, bool bDown)
{
	m_pContext->IO.KeysDown[eKey] = bDown;
}

void ImwPlatformWindowBGFX::OnChar(int iChar)
{
	m_pContext->IO.AddInputCharacter((ImwChar)iChar);
}

void ImwPlatformWindowBGFX::OnDropFiles(const EasyWindow::DropFiles& oFiles)
{
	ImVec2 oPos((float)oFiles.oPosition.x, (float)oFiles.oPosition.y);
	ImwPlatformWindow::OnDropFiles(oFiles.iCount, oFiles.pFiles, oPos);
}

#define IMGUI_FLAGS_NONE        UINT8_C(0x00)
#define IMGUI_FLAGS_ALPHA_BLEND UINT8_C(0x01)

void ImwPlatformWindowBGFX::RenderDrawLists(ImDrawData* pDrawData)
{
	bgfx::reset(uint16_t(m_pWindow->GetClientWidth()), uint16_t(m_pWindow->GetClientHeight()));
	bgfx::setViewFrameBuffer(255, m_hFrameBufferHandle);
	bgfx::setViewRect(255, 0, 0, uint16_t(m_pWindow->GetClientWidth()), uint16_t(m_pWindow->GetClientHeight()));
	bgfx::setViewMode(255, bgfx::ViewMode::Sequential);

	const ImGuiIO& io = ImGui::GetIO();
	const float width = io.DisplaySize.x;
	const float height = io.DisplaySize.y;

	{
		float ortho[16];
		bx::mtxOrtho(ortho, 0.0f, width, height, 0.0f, -1.0f, 1.0f, 0.f, false);
		bgfx::setViewTransform(255, NULL, ortho);
	}

	{ // Copy all vertices/indices to monolithic arrays
		const bgfx::Memory* pVertexBuffer = bgfx::alloc(pDrawData->TotalVtxCount * sizeof(ImDrawVert));
		const bgfx::Memory* pIndexBuffer = bgfx::alloc(pDrawData->TotalIdxCount * sizeof(ImDrawIdx));
		uint32_t iVertexOffset = 0;
		uint32_t iIndexOffset = 0;
		for (int32_t ii = 0, num = pDrawData->CmdListsCount; ii < num; ++ii)
		{
			const ImDrawList* drawList = pDrawData->CmdLists[ ii ];
			uint32_t numVertices = (uint32_t)drawList->VtxBuffer.size();
			uint32_t numIndices = (uint32_t)drawList->IdxBuffer.size();
			bx::memCopy(((ImDrawVert*)pVertexBuffer->data) + iVertexOffset, drawList->VtxBuffer.begin(), numVertices * sizeof(ImDrawVert));
			bx::memCopy(((ImDrawIdx*)pIndexBuffer->data) + iIndexOffset, drawList->IdxBuffer.begin(), numIndices * sizeof(ImDrawIdx));

			iVertexOffset += numVertices;
			iIndexOffset += numIndices;
		}

		bgfx::updateDynamicVertexBuffer(m_hVertexBuffer, 0, pVertexBuffer);
		bgfx::updateDynamicIndexBuffer(m_hIndexBuffer, 0, pIndexBuffer);
	}

	// Render command lists
	uint32_t iVertexOffset = 0;
	uint32_t iIndexOffset = 0;
	for (int32_t ii = 0, num = pDrawData->CmdListsCount; ii < num; ++ii)
	{
		const ImDrawList* drawList = pDrawData->CmdLists[ii];
		uint32_t numVertices = (uint32_t)drawList->VtxBuffer.size();
		for (const ImDrawCmd* cmd = drawList->CmdBuffer.begin(), *cmdEnd = drawList->CmdBuffer.end(); cmd != cmdEnd; ++cmd)
		{
			if (cmd->UserCallback)
			{
				cmd->UserCallback(drawList, cmd);
			}
			else if (0 != cmd->ElemCount)
			{
				uint64_t state = 0
					| BGFX_STATE_RGB_WRITE
					| BGFX_STATE_ALPHA_WRITE
					| BGFX_STATE_MSAA
					;

				bgfx::TextureHandle th = m_hTexture;
				bgfx::ProgramHandle program = m_hProgram;

				if (NULL != cmd->TextureId)
				{
					union { ImTextureID ptr; struct { bgfx::TextureHandle handle; uint8_t flags; uint8_t mip; } s; } texture = { cmd->TextureId };
					state |= 0 != (IMGUI_FLAGS_ALPHA_BLEND & texture.s.flags)
						? BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA)
						: BGFX_STATE_NONE
						;
					th = texture.s.handle;
				}
				else
				{
					state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
				}

				const uint16_t xx = uint16_t(bx::fmax(cmd->ClipRect.x, 0.0f));
				const uint16_t yy = uint16_t(bx::fmax(cmd->ClipRect.y, 0.0f));
				bgfx::setScissor(xx, yy
					, uint16_t(bx::fmin(cmd->ClipRect.z, 65535.0f) - xx)
					, uint16_t(bx::fmin(cmd->ClipRect.w, 65535.0f) - yy)
				);

				bgfx::setState(state);
				bgfx::setTexture(0, m_hUniformTexture, th);

				bgfx::setVertexBuffer(0, m_hVertexBuffer, iVertexOffset, numVertices);
				bgfx::setIndexBuffer(m_hIndexBuffer, iIndexOffset, cmd->ElemCount);
				bgfx::submit(255, program);
			}

			iIndexOffset += cmd->ElemCount;
		}
		iVertexOffset += numVertices;
	}

	bgfx::frame();
}