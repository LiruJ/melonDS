/*
	Copyright 2016-2025 melonDS team

	This file is part of melonDS.

	melonDS is free software: you can redistribute it and/or modify it under
	the terms of the GNU General Public License as published by the Free
	Software Foundation, either version 3 of the License, or (at your option)
	any later version.

	melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
	FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#include <optional>
#include <vector>
#include <string>
#include <algorithm>

#include <SDL2/SDL.h>

#include "main.h"

#include "types.h"
#include "version.h"

#include "ScreenLayout.h"

#include "Args.h"
#include "NDS.h"
#include "NDSCart.h"
#include "GBACart.h"
#include "GPU.h"
#include "SPU.h"
#include "Wifi.h"
#include "Platform.h"
#include "LocalMP.h"
#include "Config.h"
#include "RTC.h"
#include "DSi.h"
#include "DSi_I2C.h"
#include "GPU3D_Soft.h"

#include "cucumber/MessageHeaderData.h"
#include "cucumber/MemoryReadRequestMessage.h"
#include "cucumber/MemoryReadRequestData.h"
#include "cucumber/FrameReadRequestMessage.h"
#include "cucumber/FrameReadResponseMessage.h"
#include "cucumber/FrameReadRequestData.h"
#include "cucumber/InputRequestMessage.h"
#include "cucumber/InputRequestData.h"

#include "Savestate.h"

#include "EmuInstance.h"

using namespace melonDS;


EmuThread::EmuThread(EmuInstance* inst, QObject* parent) : QThread(parent)
{
	emuInstance = inst;

	emuStatus = emuStatus_Paused;
	emuPauseStack = emuPauseStackRunning;
	emuActive = false;
}

EmuThread::~EmuThread()
{
	if (serverConnection)
	{
		delete serverConnection;
		serverConnection = nullptr;
	}
}

void EmuThread::attachWindow(MainWindow* window)
{
	connect(this, SIGNAL(windowTitleChange(QString)), window, SLOT(onTitleUpdate(QString)));
	connect(this, SIGNAL(windowEmuStart()), window, SLOT(onEmuStart()));
	connect(this, SIGNAL(windowEmuStop()), window, SLOT(onEmuStop()));
	connect(this, SIGNAL(windowEmuPause(bool)), window, SLOT(onEmuPause(bool)));
	connect(this, SIGNAL(windowEmuReset()), window, SLOT(onEmuReset()));
	connect(this, SIGNAL(autoScreenSizingChange(int)), window->panel, SLOT(onAutoScreenSizingChanged(int)));
	connect(this, SIGNAL(windowFullscreenToggle()), window, SLOT(onFullscreenToggled()));
	connect(this, SIGNAL(screenEmphasisToggle()), window, SLOT(onScreenEmphasisToggled()));

	if (window->winHasMenu())
	{
		connect(this, SIGNAL(windowLimitFPSChange()), window->actLimitFramerate, SLOT(trigger()));
		connect(this, SIGNAL(swapScreensToggle()), window->actScreenSwap, SLOT(trigger()));
	}
}

void EmuThread::detachWindow(MainWindow* window)
{
	disconnect(this, SIGNAL(windowTitleChange(QString)), window, SLOT(onTitleUpdate(QString)));
	disconnect(this, SIGNAL(windowEmuStart()), window, SLOT(onEmuStart()));
	disconnect(this, SIGNAL(windowEmuStop()), window, SLOT(onEmuStop()));
	disconnect(this, SIGNAL(windowEmuPause(bool)), window, SLOT(onEmuPause(bool)));
	disconnect(this, SIGNAL(windowEmuReset()), window, SLOT(onEmuReset()));
	disconnect(this, SIGNAL(autoScreenSizingChange(int)), window->panel, SLOT(onAutoScreenSizingChanged(int)));
	disconnect(this, SIGNAL(windowFullscreenToggle()), window, SLOT(onFullscreenToggled()));
	disconnect(this, SIGNAL(screenEmphasisToggle()), window, SLOT(onScreenEmphasisToggled()));

	if (window->winHasMenu())
	{
		disconnect(this, SIGNAL(windowLimitFPSChange()), window->actLimitFramerate, SLOT(trigger()));
		disconnect(this, SIGNAL(swapScreensToggle()), window->actScreenSwap, SLOT(trigger()));
	}
}

bool EmuThread::ConnectToServer(const wchar_t* pipeName)
{
	printf("Starting connection to server\n");
	serverConnection = new cucumberDS::ServerInstance(pipeName);
	printf("Awaiting connection to server\n");
	return serverConnection->Initialise();
}

void EmuThread::run()
{
	Config::Table& globalCfg = emuInstance->getGlobalConfig();
	u32 mainScreenPos[3];

	//emuInstance->updateConsole();
	// No carts are inserted when melonDS first boots

	mainScreenPos[0] = 0;
	mainScreenPos[1] = 0;
	mainScreenPos[2] = 0;
	autoScreenSizing = 0;

	//videoSettingsDirty = false;

	//updateRenderer();
	videoSettingsDirty = true;

	u32 nframes = 0;
	double perfCountsSec = 1.0 / SDL_GetPerformanceFrequency();
	double lastTime = SDL_GetPerformanceCounter() * perfCountsSec;
	double frameLimitError = 0.0;
	double lastMeasureTime = lastTime;

	u32 winUpdateCount = 0, winUpdateFreq = 1;
	u8 dsiVolumeLevel = 0x1F;

	char melontitle[100];

	bool fastforward = false;
	bool slowmo = false;
	emuInstance->fastForwardToggled = false;
	emuInstance->slowmoToggled = false;
	unsigned int currentStep = 0;
	cucumberDS::MessageHeaderData headerData;

	while (emuStatus != emuStatus_Exit)
	{
		serverConnection->Receive(&headerData);

		MPInterface::Get().Process();
		emuInstance->inputProcess();


		if (emuInstance->hotkeyPressed(HK_FrameLimitToggle)) emit windowLimitFPSChange();

		if (emuInstance->hotkeyPressed(HK_Pause)) emuTogglePause();
		if (emuInstance->hotkeyPressed(HK_Reset)) emuReset();
		if (emuInstance->hotkeyPressed(HK_FrameStep)) emuFrameStep();

		if (emuInstance->hotkeyPressed(HK_FullscreenToggle)) emit windowFullscreenToggle();

		if (emuInstance->hotkeyPressed(HK_SwapScreens)) emit swapScreensToggle();
		if (emuInstance->hotkeyPressed(HK_SwapScreenEmphasis)) emit screenEmphasisToggle();

		if (emuStatus == emuStatus_Running || emuStatus == emuStatus_FrameStep)
		{
			if (emuStatus == emuStatus_FrameStep) emuStatus = emuStatus_Paused;

			if (emuInstance->hotkeyPressed(HK_SolarSensorDecrease))
			{
				int level = emuInstance->nds->GBACartSlot.SetInput(GBACart::Input_SolarSensorDown, true);
				if (level != -1)
				{
					emuInstance->osdAddMessage(0, "Solar sensor level: %d", level);
				}
			}
			if (emuInstance->hotkeyPressed(HK_SolarSensorIncrease))
			{
				int level = emuInstance->nds->GBACartSlot.SetInput(GBACart::Input_SolarSensorUp, true);
				if (level != -1)
				{
					emuInstance->osdAddMessage(0, "Solar sensor level: %d", level);
				}
			}

			// update render settings if needed
			if (videoSettingsDirty)
			{
				emuInstance->renderLock.lock();

				updateRenderer();

				videoSettingsDirty = false;
				emuInstance->renderLock.unlock();
			}

			// process input and hotkeys
			emuInstance->nds->SetKeyMask(emuInstance->inputMask);

			cucumberDS::InputRequestMessage* inputRequest =
				(cucumberDS::InputRequestMessage*)serverConnection->GetPipeConnection()->GetProtocol()->GetInputMessage(cucumberDS::InputRequestMessage::Id);
			if (!inputRequest)
				printf("Missing input request\n");
			else if (inputRequest->GetHasData())
			{
				cucumberDS::InputRequestData* data = inputRequest->GetData(0);
				emuInstance->touchScreen(data->GetXPosition(), data->GetYPosition());
			}
			else
				emuInstance->releaseScreen();

			if (emuInstance->isTouching)
				emuInstance->nds->TouchScreen(emuInstance->touchX, emuInstance->touchY);
			else
				emuInstance->nds->ReleaseScreen();

			if (emuInstance->hotkeyPressed(HK_Lid))
			{
				bool lid = !emuInstance->nds->IsLidClosed();
				emuInstance->nds->SetLidClosed(lid);
				emuInstance->osdAddMessage(0, lid ? "Lid closed" : "Lid opened");
			}

			// microphone input
			emuInstance->micProcess();

			// auto screen layout
			{
				mainScreenPos[2] = mainScreenPos[1];
				mainScreenPos[1] = mainScreenPos[0];
				mainScreenPos[0] = emuInstance->nds->PowerControl9 >> 15;

				int guess;
				if (mainScreenPos[0] == mainScreenPos[2] &&
					mainScreenPos[0] != mainScreenPos[1])
				{
					// constant flickering, likely displaying 3D on both screens
					// TODO: when both screens are used for 2D only...???
					guess = screenSizing_Even;
				}
				else
				{
					if (mainScreenPos[0] == 1)
						guess = screenSizing_EmphTop;
					else
						guess = screenSizing_EmphBot;
				}

				if (guess != autoScreenSizing)
				{
					autoScreenSizing = guess;
					emit autoScreenSizingChange(autoScreenSizing);
				}
			}


			// emulate
			u32 nlines;
			if (emuInstance->nds->GPU.GetRenderer3D().NeedsShaderCompile())
			{
				compileShaders();
				nlines = 1;
			}
			else
			{
				nlines = emuInstance->nds->RunFrame();
			}

			if (emuInstance->ndsSave)
				emuInstance->ndsSave->CheckFlush();

			if (emuInstance->gbaSave)
				emuInstance->gbaSave->CheckFlush();

			if (emuInstance->firmwareSave)
				emuInstance->firmwareSave->CheckFlush();

			frontBufferLock.lock();
			frontBuffer = emuInstance->nds->GPU.FrontBuffer;
			frontBufferLock.unlock();

			cucumberDS::FrameReadRequestMessage* frameRequest =
				(cucumberDS::FrameReadRequestMessage*)serverConnection->GetPipeConnection()->GetProtocol()->GetInputMessage(cucumberDS::FrameReadRequestMessage::Id);
			if (!frameRequest)
				printf("Missing frame request\n");
			else if (frameRequest->GetHasData())
			{
				cucumberDS::FrameReadResponseMessage* frameResponse =
					(cucumberDS::FrameReadResponseMessage*)serverConnection->GetPipeConnection()->GetProtocol()->GetOutputMessage(cucumberDS::FrameReadResponseMessage::Id);

				for (unsigned char i = 0; i < frameRequest->GetDataCount(); i++)
				{
					if (nullptr == frameResponse)
					{
						printf("Frame response missing\n");
						break;
					}
					cucumberDS::FrameReadRequestData* requestData = frameRequest->GetData(i);
					if (nullptr == emuInstance->nds->GPU.Framebuffer[frontBuffer][requestData->GetScreenId()])
					{
						printf("Frame buffer %d missing\n", requestData->GetScreenId());
						continue;
					}

					if (!frameResponse->Write((unsigned char*)emuInstance->nds->GPU.Framebuffer[frontBuffer][requestData->GetScreenId()].get(), requestData->GetScreenId()))
						printf("Writing for screen %d failed\n", requestData->GetScreenId());
				}
			}

#ifdef MELONCAP
			MelonCap::Update();
#endif // MELONCAP

			winUpdateCount++;
			if (winUpdateCount >= winUpdateFreq)
			{
				emit windowUpdate();
				winUpdateCount = 0;
			}

			if (emuInstance->hotkeyPressed(HK_FastForwardToggle)) emuInstance->fastForwardToggled = !emuInstance->fastForwardToggled;
			if (emuInstance->hotkeyPressed(HK_SlowMoToggle)) emuInstance->slowmoToggled = !emuInstance->slowmoToggled;

			bool enablefastforward = emuInstance->hotkeyDown(HK_FastForward) | emuInstance->fastForwardToggled;
			bool enableslowmo = emuInstance->hotkeyDown(HK_SlowMo) | emuInstance->slowmoToggled;

			fastforward = enablefastforward;
			slowmo = enableslowmo;

			if (slowmo) emuInstance->curFPS = emuInstance->slowmoFPS;
			else if (fastforward) emuInstance->curFPS = emuInstance->fastForwardFPS;
			else if (!emuInstance->doLimitFPS && !emuInstance->doAudioSync) emuInstance->curFPS = 1000.0;
			else emuInstance->curFPS = emuInstance->targetFPS;

			if (emuInstance->doAudioSync && !(fastforward || slowmo))
				emuInstance->audioSync();

			double frametimeStep = nlines / (emuInstance->curFPS * 263.0);

			if (frametimeStep < 0.001) frametimeStep = 0.001;

			if (emuInstance->doLimitFPS)
			{
				double curtime = SDL_GetPerformanceCounter() * perfCountsSec;

				frameLimitError += frametimeStep - (curtime - lastTime);
				if (frameLimitError < -frametimeStep)
					frameLimitError = -frametimeStep;
				if (frameLimitError > frametimeStep)
					frameLimitError = frametimeStep;

				if (round(frameLimitError * 1000.0) > 0.0)
				{
					SDL_Delay(round(frameLimitError * 1000.0));
					double timeBeforeSleep = curtime;
					curtime = SDL_GetPerformanceCounter() * perfCountsSec;
					frameLimitError -= curtime - timeBeforeSleep;
				}

				lastTime = curtime;
			}

			nframes++;
			if (nframes >= 30)
			{
				double time = SDL_GetPerformanceCounter() * perfCountsSec;
				double dt = time - lastMeasureTime;
				lastMeasureTime = time;

				u32 fps = round(nframes / dt);
				nframes = 0;

				float fpstarget = 1.0 / frametimeStep;

				winUpdateFreq = fps / (u32)round(fpstarget);
				if (winUpdateFreq < 1)
					winUpdateFreq = 1;

				double actualfps = (59.8261 * 263.0) / nlines;
				int inst = emuInstance->instanceID;
				if (inst == 0)
					snprintf(melontitle, sizeof(melontitle), "[%d/%.0f] melonDS " MELONDS_VERSION, fps, actualfps);
				else
					snprintf(melontitle, sizeof(melontitle), "[%d/%.0f] melonDS (%d)", fps, actualfps, inst + 1);
				changeWindowTitle(melontitle);
			}
		}
		else
		{
			// paused
			nframes = 0;
			lastTime = SDL_GetPerformanceCounter() * perfCountsSec;
			lastMeasureTime = lastTime;

			emit windowUpdate();

			int inst = emuInstance->instanceID;
			if (inst == 0)
				snprintf(melontitle, sizeof(melontitle), "melonDS " MELONDS_VERSION);
			else
				snprintf(melontitle, sizeof(melontitle), "melonDS (%d)", inst + 1);
			changeWindowTitle(melontitle);

			SDL_Delay(75);
		}

		cucumberDS::MemoryReadRequestMessage* memoryRequest =
			(cucumberDS::MemoryReadRequestMessage*)serverConnection->GetPipeConnection()->GetProtocol()->GetInputMessage(cucumberDS::MemoryReadRequestMessage::Id);
		if (memoryRequest && memoryRequest->GetHasData() && nullptr != emuInstance->nds && nullptr != emuInstance->nds->MainRAM)
		{
			u8* ndsRam = emuInstance->nds->MainRAM;
			for (unsigned char i = 0; i < memoryRequest->GetDataCount(); i++)
			{
				cucumberDS::MemoryReadRequestData* requestData = memoryRequest->GetData(i);
				unsigned int address = requestData->GetAddress();
				//printf("memory address %#010x\n", address & emuInstance->nds->MainRAMMask);
				//printf("Value at memory address %#010x is %d\n", address, *(unsigned int*)(ndsRam + (address&emuInstance->nds->MainRAMMask)));
				//printf("Value at memory address %#010x is %s\n", address, (char*)(ndsRam + (address & emuInstance->nds->MainRAMMask)));
			}
		}

		serverConnection->Send(currentStep);
		currentStep++;

		handleMessages();
	}
}

void EmuThread::sendMessage(Message msg)
{
	msgMutex.lock();
	msgQueue.enqueue(msg);
	msgMutex.unlock();
}

void EmuThread::waitMessage(int num)
{
	if (QThread::currentThread() == this) return;
	msgSemaphore.acquire(num);
}

void EmuThread::waitAllMessages()
{
	if (QThread::currentThread() == this) return;
	while (!msgQueue.empty())
		msgSemaphore.acquire();
}

void EmuThread::handleMessages()
{
	msgMutex.lock();
	while (!msgQueue.empty())
	{
		Message msg = msgQueue.dequeue();
		switch (msg.type)
		{
		case msg_Exit:
			emuStatus = emuStatus_Exit;
			emuPauseStack = emuPauseStackRunning;

			emuInstance->audioDisable();
			MPInterface::Get().End(emuInstance->instanceID);
			break;

		case msg_EmuRun:
			emuStatus = emuStatus_Running;
			emuPauseStack = emuPauseStackRunning;
			emuActive = true;

			emuInstance->audioEnable();
			emit windowEmuStart();
			break;

		case msg_EmuPause:
			emuPauseStack++;
			if (emuPauseStack > emuPauseStackPauseThreshold) break;

			prevEmuStatus = emuStatus;
			emuStatus = emuStatus_Paused;

			if (prevEmuStatus != emuStatus_Paused)
			{
				emuInstance->audioDisable();
				emit windowEmuPause(true);
				emuInstance->osdAddMessage(0, "Paused");
			}
			break;

		case msg_EmuUnpause:
			if (emuPauseStack < emuPauseStackPauseThreshold) break;

			emuPauseStack--;
			if (emuPauseStack >= emuPauseStackPauseThreshold) break;

			emuStatus = prevEmuStatus;

			if (emuStatus != emuStatus_Paused)
			{
				emuInstance->audioEnable();
				emit windowEmuPause(false);
				emuInstance->osdAddMessage(0, "Resumed");
			}
			break;

		case msg_EmuStop:
			if (msg.param.value<bool>())
				emuInstance->nds->Stop();
			emuStatus = emuStatus_Paused;
			emuActive = false;

			emuInstance->audioDisable();
			emit windowEmuStop();
			break;

		case msg_EmuFrameStep:
			emuStatus = emuStatus_FrameStep;
			break;

		case msg_EmuReset:
			emuInstance->reset();

			emuStatus = emuStatus_Running;
			emuPauseStack = emuPauseStackRunning;
			emuActive = true;

			emuInstance->audioEnable();
			emit windowEmuReset();
			emuInstance->osdAddMessage(0, "Reset");
			break;

		case msg_BootROM:
			msgResult = 0;
			if (!emuInstance->loadROM(msg.param.value<QStringList>(), true, msgError))
				break;

			assert(emuInstance->nds != nullptr);
			emuInstance->nds->Start();
			msgResult = 1;
			break;

		case msg_BootFirmware:
			msgResult = 0;
			if (!emuInstance->bootToMenu(msgError))
				break;

			assert(emuInstance->nds != nullptr);
			emuInstance->nds->Start();
			msgResult = 1;
			break;

		case msg_InsertCart:
			msgResult = 0;
			if (!emuInstance->loadROM(msg.param.value<QStringList>(), false, msgError))
				break;

			msgResult = 1;
			break;

		case msg_EjectCart:
			emuInstance->ejectCart();
			break;

		case msg_InsertGBACart:
			msgResult = 0;
			if (!emuInstance->loadGBAROM(msg.param.value<QStringList>(), msgError))
				break;

			msgResult = 1;
			break;

		case msg_InsertGBAAddon:
			msgResult = 0;
			emuInstance->loadGBAAddon(msg.param.value<int>(), msgError);
			msgResult = 1;
			break;

		case msg_EjectGBACart:
			emuInstance->ejectGBACart();
			break;

		case msg_SaveState:
			msgResult = emuInstance->saveState(msg.param.value<QString>().toStdString());
			break;

		case msg_LoadState:
			msgResult = emuInstance->loadState(msg.param.value<QString>().toStdString());
			break;

		case msg_UndoStateLoad:
			emuInstance->undoStateLoad();
			msgResult = 1;
			break;

		case msg_ImportSavefile:
		{
			msgResult = 0;
			auto f = Platform::OpenFile(msg.param.value<QString>().toStdString(), Platform::FileMode::Read);
			if (!f) break;

			u32 len = FileLength(f);

			std::unique_ptr<u8[]> data = std::make_unique<u8[]>(len);
			Platform::FileRewind(f);
			Platform::FileRead(data.get(), len, 1, f);

			assert(emuInstance->nds != nullptr);
			emuInstance->nds->SetNDSSave(data.get(), len);

			CloseFile(f);
			msgResult = 1;
		}
		break;

		case msg_EnableCheats:
			emuInstance->enableCheats(msg.param.value<bool>());
			break;
		}

		msgSemaphore.release();
	}
	msgMutex.unlock();
}

void EmuThread::changeWindowTitle(char* title)
{
	emit windowTitleChange(QString(title));
}

void EmuThread::initContext(int win)
{
	sendMessage({ .type = msg_InitGL, .param = win });
	waitMessage();
}

void EmuThread::deinitContext(int win)
{
	sendMessage({ .type = msg_DeInitGL, .param = win });
	waitMessage();
}

void EmuThread::emuRun()
{
	sendMessage(msg_EmuRun);
	waitMessage();
}

void EmuThread::emuPause(bool broadcast)
{
	sendMessage(msg_EmuPause);
	waitMessage();

	if (broadcast)
		emuInstance->broadcastCommand(InstCmd_Pause);
}

void EmuThread::emuUnpause(bool broadcast)
{
	sendMessage(msg_EmuUnpause);
	waitMessage();

	if (broadcast)
		emuInstance->broadcastCommand(InstCmd_Unpause);
}

void EmuThread::emuTogglePause(bool broadcast)
{
	if (emuStatus == emuStatus_Paused)
		emuUnpause(broadcast);
	else
		emuPause(broadcast);
}

void EmuThread::emuStop(bool external)
{
	sendMessage({ .type = msg_EmuStop, .param = external });
	waitMessage();
}

void EmuThread::emuExit()
{
	sendMessage(msg_Exit);
	waitAllMessages();
}

void EmuThread::emuFrameStep()
{
	if (emuPauseStack < emuPauseStackPauseThreshold)
		sendMessage(msg_EmuPause);
	sendMessage(msg_EmuFrameStep);
	waitAllMessages();
}

void EmuThread::emuReset()
{
	sendMessage(msg_EmuReset);
	waitMessage();
}

bool EmuThread::emuIsRunning()
{
	return emuStatus == emuStatus_Running;
}

bool EmuThread::emuIsActive()
{
	return emuActive;
}

int EmuThread::bootROM(const QStringList& filename, QString& errorstr)
{
	sendMessage({ .type = msg_BootROM, .param = filename });
	waitMessage();
	if (!msgResult)
	{
		errorstr = msgError;
		return msgResult;
	}

	sendMessage(msg_EmuRun);
	waitMessage();
	errorstr = "";
	return msgResult;
}

int EmuThread::bootFirmware(QString& errorstr)
{
	sendMessage(msg_BootFirmware);
	waitMessage();
	if (!msgResult)
	{
		errorstr = msgError;
		return msgResult;
	}

	sendMessage(msg_EmuRun);
	waitMessage();
	errorstr = "";
	return msgResult;
}

int EmuThread::insertCart(const QStringList& filename, bool gba, QString& errorstr)
{
	MessageType msgtype = gba ? msg_InsertGBACart : msg_InsertCart;

	sendMessage({ .type = msgtype, .param = filename });
	waitMessage();
	errorstr = msgResult ? "" : msgError;
	return msgResult;
}

void EmuThread::ejectCart(bool gba)
{
	sendMessage(gba ? msg_EjectGBACart : msg_EjectCart);
	waitMessage();
}

int EmuThread::insertGBAAddon(int type, QString& errorstr)
{
	sendMessage({ .type = msg_InsertGBAAddon, .param = type });
	waitMessage();
	errorstr = msgResult ? "" : msgError;
	return msgResult;
}

int EmuThread::saveState(const QString& filename)
{
	sendMessage({ .type = msg_SaveState, .param = filename });
	waitMessage();
	return msgResult;
}

int EmuThread::loadState(const QString& filename)
{
	sendMessage({ .type = msg_LoadState, .param = filename });
	waitMessage();
	return msgResult;
}

int EmuThread::undoStateLoad()
{
	sendMessage(msg_UndoStateLoad);
	waitMessage();
	return msgResult;
}

int EmuThread::importSavefile(const QString& filename)
{
	sendMessage(msg_EmuReset);
	sendMessage({ .type = msg_ImportSavefile, .param = filename });
	waitMessage(2);
	return msgResult;
}

void EmuThread::enableCheats(bool enable)
{
	sendMessage({ .type = msg_EnableCheats, .param = enable });
	waitMessage();
}

void EmuThread::updateRenderer()
{
	if (!videoSettingsInitialised)
	{
		emuInstance->nds->GPU.SetRenderer3D(std::make_unique<SoftRenderer>());
		videoSettingsInitialised = true;
	}

	auto& cfg = emuInstance->getGlobalConfig();
	static_cast<SoftRenderer&>(emuInstance->nds->GPU.GetRenderer3D()).SetThreaded(
		cfg.GetBool("3D.Soft.Threaded"),
		emuInstance->nds->GPU);
}

void EmuThread::compileShaders()
{
	int currentShader, shadersCount;
	u64 startTime = SDL_GetPerformanceCounter();
	// kind of hacky to look at the wallclock, though it is easier than
	// than disabling vsync
	do
	{
		emuInstance->nds->GPU.GetRenderer3D().ShaderCompileStep(currentShader, shadersCount);
	} while (emuInstance->nds->GPU.GetRenderer3D().NeedsShaderCompile() &&
		(SDL_GetPerformanceCounter() - startTime) * perfCountsSec < 1.0 / 6.0);
	emuInstance->osdAddMessage(0, "Compiling shader %d/%d", currentShader + 1, shadersCount);
}
