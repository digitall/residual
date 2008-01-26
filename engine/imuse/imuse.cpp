/* Residual - Virtual machine to run LucasArts' 3D adventure games
 * Copyright (C) 2003-2006 The ScummVM-Residual Team (www.scummvm.org)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.

 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 *
 * $URL$
 * $Id$
 *
 */

#include "stdafx.h"
#include "bits.h"
#include "debug.h"
#include "timer.h"
#include "engine.h"
#include "savegame.h"

#include "mixer/mixer.h"
#include "mixer/audiostream.h"

#include "imuse/imuse.h"
#include "imuse/imuse_sndmgr.h"

Imuse *g_imuse = NULL;

extern uint16 imuseDestTable[];
extern ImuseTable grimStateMusicTable[];
extern ImuseTable grimSeqMusicTable[];
extern ImuseTable grimDemoStateMusicTable[];
extern ImuseTable grimDemoSeqMusicTable[];

Imuse::Track::Track()
	: used(false), stream(NULL) {
}

void Imuse::timerHandler(void *refCon) {
	Imuse *imuse = (Imuse *)refCon;
	imuse->callback();
}

Imuse::Imuse(int fps) {
	_mutex = g_driver->createMutex();
	_pause = false;
	_sound = new ImuseSndMgr();
	_volVoice = 0;
	_volSfx = 0;
	_volMusic = 0;
	_callbackFps = fps;
	resetState();
	for (int l = 0; l < MAX_IMUSE_TRACKS + MAX_IMUSE_FADETRACKS; l++) {
		_track[l] = new Track;
		_track[l]->trackId = l;
		_track[l]->used = false;
		strcpy(_track[l]->soundName, "");
	}
	vimaInit(imuseDestTable);
	_stateMusicTable = grimStateMusicTable;
	_seqMusicTable = grimSeqMusicTable;
	g_timer->installTimerProc(timerHandler, 1000000 / _callbackFps, this);
}

Imuse::~Imuse() {
	stopAllSounds();
	g_timer->removeTimerProc(timerHandler);
	for (int l = 0; l < MAX_IMUSE_TRACKS + MAX_IMUSE_FADETRACKS; l++) {
		delete _track[l];
	}
	delete _sound;
	g_driver->deleteMutex(_mutex);
}

void Imuse::resetState() {
	_curMusicState = 0;
	_curMusicSeq = 0;
	memset(_attributes, 0, sizeof(_attributes));
}

void Imuse::restoreState(SaveGame *savedState) {
	StackLock lock(_mutex);
	printf("Imuse::restoreState() started.\n");

	savedState->beginSection('IMUS');
	savedState->read(&_volVoice, sizeof(int32));
	savedState->read(&_volSfx, sizeof(int32));
	savedState->read(&_volMusic, sizeof(int32));
	savedState->read(&_curMusicState, sizeof(int32));
	savedState->read(&_curMusicSeq, sizeof(int32));
	savedState->read(_attributes, sizeof(int32) * 185);

	for (int l = 0; l < MAX_IMUSE_TRACKS + MAX_IMUSE_FADETRACKS; l++) {
		Track *track = _track[l];
		savedState->read(&track->pan, sizeof(int32));
		savedState->read(&track->panFadeDest, sizeof(int32));
		savedState->read(&track->panFadeDelay, sizeof(int32));
		savedState->read(&track->panFadeUsed, sizeof(bool));
		savedState->read(&track->vol, sizeof(int32));
		savedState->read(&track->volFadeDest, sizeof(int32));
		savedState->read(&track->volFadeDelay, sizeof(int32));
		savedState->read(&track->volFadeUsed, sizeof(bool));
		savedState->read(track->soundName, 32);
		savedState->read(&track->used, sizeof(bool));
		savedState->read(&track->toBeRemoved, sizeof(bool));
		savedState->read(&track->readyToRemove, sizeof(bool));
		savedState->read(&track->started, sizeof(bool));
		savedState->read(&track->priority, sizeof(int32));
		savedState->read(&track->regionOffset, sizeof(int32));
		savedState->read(&track->dataOffset, sizeof(int32));
		savedState->read(&track->curRegion, sizeof(int32));
		savedState->read(&track->curHookId, sizeof(int32));
		savedState->read(&track->volGroupId, sizeof(int32));
		savedState->read(&track->iteration, sizeof(int32));
		savedState->read(&track->mixerFlags, sizeof(int32));
		savedState->read(&track->mixerVol, sizeof(int32));
		savedState->read(&track->mixerPan, sizeof(int32));

		if (!track->used)
			continue;

		track->readyToRemove = false;
		if (track->toBeRemoved) {
			track->stream = NULL;
			track->used = false;
			continue;
		}

		track->soundHandle = _sound->openSound(track->soundName, track->volGroupId);
		assert(track->soundHandle);

		int32 streamBufferSize = track->iteration;
		int	freq = _sound->getFreq(track->soundHandle);

		track->stream = makeAppendableAudioStream(freq, track->mixerFlags, streamBufferSize);
		g_mixer->playInputStream(&track->handle, track->stream, false, -1, track->mixerVol, track->mixerPan, false);
	}
	savedState->endSection();
	printf("Imuse::restoreState() finished.\n");
}

void Imuse::saveState(SaveGame *savedState) {
	StackLock lock(_mutex);
	printf("Imuse::saveState() started.\n");

	savedState->beginSection('IMUS');
	savedState->write(&_volVoice, sizeof(int32));
	savedState->write(&_volSfx, sizeof(int32));
	savedState->write(&_volMusic, sizeof(int32));
	savedState->write(&_curMusicState, sizeof(int32));
	savedState->write(&_curMusicSeq, sizeof(int32));
	savedState->write(_attributes, sizeof(int32) * 185);

	for (int l = 0; l < MAX_IMUSE_TRACKS + MAX_IMUSE_FADETRACKS; l++) {
		Track *track = _track[l];
		savedState->write(&track->pan, sizeof(int32));
		savedState->write(&track->panFadeDest, sizeof(int32));
		savedState->write(&track->panFadeDelay, sizeof(int32));
		savedState->write(&track->panFadeUsed, sizeof(bool));
		savedState->write(&track->vol, sizeof(int32));
		savedState->write(&track->volFadeDest, sizeof(int32));
		savedState->write(&track->volFadeDelay, sizeof(int32));
		savedState->write(&track->volFadeUsed, sizeof(bool));
		savedState->write(track->soundName, 32);
		savedState->write(&track->used, sizeof(bool));
		savedState->write(&track->toBeRemoved, sizeof(bool));
		savedState->write(&track->readyToRemove, sizeof(bool));
		savedState->write(&track->started, sizeof(bool));
		savedState->write(&track->priority, sizeof(int32));
		savedState->write(&track->regionOffset, sizeof(int32));
		savedState->write(&track->dataOffset, sizeof(int32));
		savedState->write(&track->curRegion, sizeof(int32));
		savedState->write(&track->curHookId, sizeof(int32));
		savedState->write(&track->volGroupId, sizeof(int32));
		savedState->write(&track->iteration, sizeof(int32));
		savedState->write(&track->mixerFlags, sizeof(int32));
		savedState->write(&track->mixerVol, sizeof(int32));
		savedState->write(&track->mixerPan, sizeof(int32));
	}
	savedState->endSection();
	printf("Imuse::saveState() finished.\n");
}

void Imuse::callback() {
	StackLock lock(_mutex);

	for (int l = 0; l < MAX_IMUSE_TRACKS + MAX_IMUSE_FADETRACKS; l++) {
		Track *track = _track[l];
		if (track->used && !track->readyToRemove) {
			if (track->toBeRemoved) {
				track->readyToRemove = true;
				continue;
			}

			if (_pause)
				return;

			if (track->volFadeUsed) {
				if (track->volFadeStep < 0) {
					if (track->vol > track->volFadeDest) {
						track->vol += track->volFadeStep;
						if (track->vol < track->volFadeDest) {
							track->vol = track->volFadeDest;
							track->volFadeUsed = false;
						}
						if (track->vol == 0) {
							track->toBeRemoved = true;
						}
					}
				} else if (track->volFadeStep > 0) {
					if (track->vol < track->volFadeDest) {
						track->vol += track->volFadeStep;
						if (track->vol > track->volFadeDest) {
							track->vol = track->volFadeDest;
							track->volFadeUsed = false;
						}
					}
				}
			}

			if (track->panFadeUsed) {
				if (track->panFadeStep < 0) {
					if (track->pan > track->panFadeDest) {
						track->pan += track->panFadeStep;
						if (track->pan < track->panFadeDest) {
							track->pan = track->panFadeDest;
							track->panFadeUsed = false;
						}
					}
				} else if (track->panFadeStep > 0) {
					if (track->pan < track->panFadeDest) {
						track->pan += track->panFadeStep;
						if (track->pan > track->panFadeDest) {
							track->pan = track->panFadeDest;
							track->panFadeUsed = false;
						}
					}
				}
			}

			int pan = track->pan / 1000;
			pan = (pan != 64) ? 2 * pan - 127 : 0;
			int vol = track->vol / 1000;

			if (track->volGroupId == 1)
				vol = (vol * _volVoice) / 128;
			if (track->volGroupId == 2)
				vol = (vol * _volSfx) / 128;
			if (track->volGroupId == 3)
				vol = (vol * _volMusic) / 128;

			track->mixerVol = vol;
			track->mixerPan = pan;

			if (track->stream) {
				byte *data = NULL;
				int32 result = 0;

				if (track->curRegion == -1) {
					switchToNextRegion(track);
					if (track->toBeRemoved)
						continue;
				}

				int channels = _sound->getChannels(track->soundHandle);

				int32 mixer_size = track->iteration / _callbackFps;

				if (track->stream->endOfData()) {
					mixer_size *= 2;
				}

				if (channels == 1)
					mixer_size &= ~1;
				if (channels == 2)
					mixer_size &= ~3;

				if (mixer_size == 0)
					continue;

				do {
					result = _sound->getDataFromRegion(track->soundHandle, track->curRegion, &data, track->regionOffset, mixer_size);
					if (channels == 1) {
						result &= ~1;
					}
					if (channels == 2) {
						result &= ~3;
					}

					if (result > mixer_size)
						result = mixer_size;

					if (g_mixer->isReady()) {
						g_mixer->setChannelVolume(track->handle, vol);
						g_mixer->setChannelBalance(track->handle, pan);
						track->stream->append(data, result);
						track->regionOffset += result;
						free(data);
					}

					if (_sound->isEndOfRegion(track->soundHandle, track->curRegion)) {
						switchToNextRegion(track);
						if (track->toBeRemoved)
							break;
					}
					mixer_size -= result;
					assert(mixer_size >= 0);
				} while (mixer_size != 0);
			}
		}
	}
}

void Imuse::switchToNextRegion(Track *track) {
	assert(track);

	if (track->trackId >= MAX_IMUSE_TRACKS) {
		track->toBeRemoved = true;
		return;
	}

	int numRegions = _sound->getNumRegions(track->soundHandle);

	if (++track->curRegion == numRegions) {
		track->toBeRemoved = true;
		return;
	}

	ImuseSndMgr::SoundStruct *soundHandle = track->soundHandle;
	int jumpId = _sound->getJumpIdByRegionAndHookId(soundHandle, track->curRegion, track->curHookId);
	if (jumpId == -1)
		jumpId = _sound->getJumpIdByRegionAndHookId(soundHandle, track->curRegion, 0);
	if (jumpId != -1) {
		int region = _sound->getRegionIdByJumpId(soundHandle, jumpId);
		assert(region != -1);
		int sampleHookId = _sound->getJumpHookId(soundHandle, jumpId);
		assert(sampleHookId != -1);
		int fadeDelay = (60 * _sound->getJumpFade(soundHandle, jumpId)) / 1000;
		if (sampleHookId != 0) {
			if (track->curHookId == sampleHookId) {
				if (fadeDelay != 0) {
					Track *fadeTrack = cloneToFadeOutTrack(track, fadeDelay);
					fadeTrack->dataOffset = _sound->getRegionOffset(fadeTrack->soundHandle, fadeTrack->curRegion);
					fadeTrack->regionOffset = 0;
					fadeTrack->curHookId = 0;
				}
				track->curRegion = region;
				track->curHookId = 0;
			}
		} else {
			if (fadeDelay != 0) {
				Track *fadeTrack = cloneToFadeOutTrack(track, fadeDelay);
				fadeTrack->dataOffset = _sound->getRegionOffset(fadeTrack->soundHandle, fadeTrack->curRegion);
				fadeTrack->regionOffset = 0;
			}
			track->curRegion = region;
			if (track->curHookId == 0x80) {
				track->curHookId = 0;
			}
		}
	}

	track->dataOffset = _sound->getRegionOffset(soundHandle, track->curRegion);
	track->regionOffset = 0;
}