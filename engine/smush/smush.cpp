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
#include "smush.h"
#include "timer.h"
#include "resource.h"
#include "engine.h"
#include "driver.h"

#include "mixer/mixer.h"

#include <cstring>
#include <zlib.h>

#define SMUSH_LOOPMOVIE(x)		(x & 0x000001)
#define SMUSH_ALTSPEED(x)		(x & 0x000004)

#define ANNO_HEADER "MakeAnim animation type 'Bl16' parameters: "
#define BUFFER_SIZE 16385

Smush *g_smush;
static uint16 smushDestTable[5786];

void Smush::timerCallback(void *) {
	g_smush->handleFrame();
}

Smush::Smush() {
	g_smush = this;
	_nbframes = 0;
	_internalBuffer = NULL;
	_externalBuffer = NULL;
	_width = 0;
	_height = 0;
	_speed = 0;
	_channels = -1;
	_freq = 22050;
	_videoFinished = false;
	_videoLooping = false;
	_videoPause = true;
	_updateNeeded = false;
	_startPos = NULL;
	_stream = NULL;
	_movieTime = 0;
	_frame = 0;
}

Smush::~Smush() {
	deinit();
}

void Smush::init() {
	_stream = NULL;
	_frame = 0;
	_movieTime = 0;
	_videoFinished = false;
	_videoPause = false;
	_updateNeeded = false;

	assert(!_internalBuffer);
	assert(!_externalBuffer);

	_internalBuffer = (byte *)malloc(_width * _height * 2);
	_externalBuffer = (byte *)malloc(_width * _height * 2);

	vimaInit(smushDestTable);
	g_timer->installTimerProc(&timerCallback, _speed, NULL);
}

void Smush::deinit() {
	g_timer->removeTimerProc(&timerCallback);

	if (_internalBuffer) {
		free(_internalBuffer);
		_internalBuffer = NULL;
	}
	if (_externalBuffer) {
		free(_externalBuffer);
		_externalBuffer = NULL;
	}
	if (_videoLooping && _startPos != NULL) {
		free(_startPos->tmpBuf);
		free(_startPos);
		_startPos = NULL;
	}
	if (_stream) {
		_stream->finish();
		_stream = NULL;
		g_mixer->stopHandle(_soundHandle);
	}
	_videoLooping = false;
	_videoFinished = true;
	_videoPause = true;
 	_file.close();
}

void Smush::handleWave(const byte *src, uint32 size) {
	int16 *dst = (int16 *)malloc(size * _channels * 2);
	decompressVima(src, dst, size * _channels * 2, smushDestTable);

	int flags = SoundMixer::FLAG_16BITS;
	if (_channels == 2)
		flags |= SoundMixer::FLAG_STEREO;

	if (!_stream) {
		_stream = makeAppendableAudioStream(_freq, flags, 500000);
		g_mixer->playInputStream(&_soundHandle, _stream, true);
	}
 	if (_stream)
		_stream->append((byte *)dst, size * _channels * 2);
	free(dst);
}

void Smush::handleFrame() {
	uint32 tag;
	int32 size;
	int pos = 0;

	if (_videoPause)
		return;

	if (_videoFinished) {
		_videoPause = true;
		return;
	}

	if (_frame == _nbframes) {
		// If the video has been looping and was previously on the last
		// frame then reset the frame number and the movie time, this
		// needs to occur at the beginning so the last frame has time to
		// render appropriately
		_frame = 0;
		_movieTime = 0;
	}

	tag = _file.readUint32BE();
	if (tag == MKID_BE('ANNO')) {
		char *anno;
		byte *data;
		
		size = _file.readUint32BE();
		data = (byte *)malloc(size);
		_file.read(data, size);
		anno = (char *)data;
		if (strncmp(anno, ANNO_HEADER, sizeof(ANNO_HEADER)-1) == 0) {
			//char *annoData = anno + sizeof(ANNO_HEADER);

			// Examples:
			//  Water streaming around boat from Manny's balcony
			//  MakeAnim animation type 'Bl16' parameters: 10000;12000;100;1;0;0;0;0;25;0;
			//  Water in front of the Blue Casket
			//  MakeAnim animation type 'Bl16' parameters: 20000;25000;100;1;0;0;0;0;25;0;
			//  Scrimshaw exterior:
			//  MakeAnim animation type 'Bl16' parameters: 6000;8000;100;0;0;0;0;0;2;0;
			//  Lola engine room (loops a limited number of times?):
			//  MakeAnim animation type 'Bl16' parameters: 6000;8000;90;1;0;0;0;0;2;0;
			if (debugLevel == DEBUG_SMUSH || debugLevel == DEBUG_NORMAL || debugLevel == DEBUG_ALL)
				printf("Announcement data: %s\n", anno);
			// It looks like the announcement data is actually for setting some of the
			// header parameters, not for any looping purpose
		} else {
			if (debugLevel == DEBUG_SMUSH || debugLevel == DEBUG_NORMAL || debugLevel == DEBUG_ALL)
				printf("Announcement header not understood: %s\n", anno);
		}
		free(anno);
		tag = _file.readUint32BE();
	}

	assert(tag == MKID_BE('FRME'));
	size = _file.readUint32BE();
	byte *frame = (byte *)malloc(size);
	_file.read(frame, size);

	do {
		if (READ_BE_UINT32(frame + pos) == MKID_BE('Bl16')) {
			_blocky16.decode(_internalBuffer, frame + pos + 8);
			pos += READ_BE_UINT32(frame + pos + 4) + 8;
		} else if (READ_BE_UINT32(frame + pos) == MKID_BE('Wave')) {
			int decompressed_size = READ_BE_UINT32(frame + pos + 8);
			if (decompressed_size < 0)
				handleWave(frame + pos + 8 + 4 + 8, READ_BE_UINT32(frame + pos + 8 + 8));
			else
				handleWave(frame + pos + 8 + 4, decompressed_size);
			pos += READ_BE_UINT32(frame + pos + 4) + 8;
		} else if (debugLevel == DEBUG_SMUSH || debugLevel == DEBUG_ERROR || debugLevel == DEBUG_ALL) {
			error("Smush::handleFrame() unknown tag");
		}
	} while (pos < size);
	free(frame);

	memcpy(_externalBuffer, _internalBuffer, _width * _height * 2);
	_updateNeeded = true;

	_frame++;
	_movieTime += _speed / 1000;
	if (_frame == _nbframes) {
		// If we're not supposed to loop (or looping fails) then end the video
		if(!_videoLooping || !_file.setPos(_startPos)) {
			_videoFinished = true;
			g_engine->setMode(ENGINE_MODE_NORMAL);
			return;
		}
	}
}

void Smush::handleFramesHeader() {
	uint32 tag;
	int32 size;
	int pos = 0;

	tag = _file.readUint32BE();
	assert(tag == MKID_BE('FLHD'));
	size = _file.readUint32BE();
	byte *f_header = (byte*)malloc(size);
	_file.read(f_header, size);

	do {
		if (READ_BE_UINT32(f_header + pos) == MKID_BE('Bl16')) {
			pos += READ_BE_UINT32(f_header + pos + 4) + 8;
		} else if (READ_BE_UINT32(f_header + pos) == MKID_BE('Wave')) {
			_freq = READ_LE_UINT32(f_header + pos + 8);
			_channels = READ_LE_UINT32(f_header + pos + 12);
			pos += 20;
		} else if (debugLevel == DEBUG_SMUSH || debugLevel == DEBUG_ERROR || debugLevel == DEBUG_ALL){
			error("Smush::handleFramesHeader() unknown tag");
		}
	} while (pos < size);
	free(f_header);
}

bool Smush::setupAnim(const char *file, int x, int y) {
	uint32 tag;
	int32 size;
	int16 flags;

	if (!_file.open(file))
		return false;

	tag = _file.readUint32BE();
	assert(tag == MKID_BE('SANM'));

	size = _file.readUint32BE();
	tag = _file.readUint32BE();
	assert(tag == MKID_BE('SHDR'));

	size = _file.readUint32BE();
	byte *s_header = (byte *)malloc(size);
	_file.read(s_header, size);
	_nbframes = READ_LE_UINT32(s_header + 2);
	int width = READ_LE_UINT16(s_header + 8);
	int height = READ_LE_UINT16(s_header + 10);
	if ((_width != width) || (_height != height)) {
		_blocky16.init(width, height);
	}

	_x = x;
	_y = y;
	_width = width;
	_height = height;

	_speed = READ_LE_UINT32(s_header + 14);
	flags = READ_LE_UINT16(s_header + 18);
	// Output information for checking out the flags
	if (debugLevel == DEBUG_SMUSH || debugLevel == DEBUG_NORMAL || debugLevel == DEBUG_ALL) {
		printf("SMUSH Flags:");
		for(int i = 0; i < 16; i++)
			printf(" %d", (flags & (1 << i)) != 0);
		printf("\n");
	}
	// Videos "copaldie.snm" and "getshcks.snm" seem to have
	// the wrong speed value, the value 66667 (used by the
	// other videos) seems to work whereas "2x" (68928)
	// does not quite do it.
	// TODO: Find out what needs to go on here.
	if (SMUSH_ALTSPEED(flags)) {
		printf("Bad time: %d, suggested: %d\n", _speed, 2 * _speed);
		_speed = 66667;
	}
	_videoLooping = SMUSH_LOOPMOVIE(flags);
	_startPos = NULL; // Set later
	free(s_header);

	return true;
}

void Smush::stop() { 
	deinit();
 	g_engine->setMode(ENGINE_MODE_NORMAL);
}

bool Smush::play(const char *filename, int x, int y) {
	deinit();

	if (debugLevel == DEBUG_SMUSH)
		printf("Playing video '%s'.\n", filename);
	
	// Load the video
	if (!setupAnim(filename, x, y))
		return false;

	handleFramesHeader();
	if (_videoLooping)
		_startPos = _file.getPos();
	init();

	return true;
}

zlibFile::zlibFile() {
	_handle = NULL;
	_inBuf = NULL;
}

zlibFile::~zlibFile() {
	close();
}

struct SavePos *zlibFile::getPos() { 
	struct SavePos *pos;
	long position = std::ftell(_handle);
	
	if (position == -1) {
		if (debugLevel == DEBUG_SMUSH || debugLevel == DEBUG_WARN || debugLevel == DEBUG_ALL)
			warning("zlibFile::open() unable to find start position! %m");
		return NULL;
	}
	pos = (struct SavePos *) malloc(sizeof(struct SavePos));
	pos->filePos = position;
	inflateCopy(&pos->streamBuf, &_stream);
	pos->tmpBuf = (char *)calloc(1, BUFFER_SIZE);
	memcpy(pos->tmpBuf, _inBuf, BUFFER_SIZE);
	return pos;
}

bool zlibFile::setPos(struct SavePos *pos) { 
	int ret;
	
	if (pos == NULL) {
		warning("Unable to rewind SMUSH movie (no position passed)!");
		return false;
	}
	if (_handle == NULL) {
		warning("Unable to rewind SMUSH movie (invalid handle)!");
		return false;
	}
	ret = std::fseek(_handle, pos->filePos, SEEK_SET);
	if (ret == -1) {
		warning("Unable to rewind SMUSH movie (seek failed)! %m");
		return false;
	}
	memcpy(_inBuf, pos->tmpBuf, BUFFER_SIZE);
	if (inflateCopy(&_stream, &pos->streamBuf) != Z_OK) {
		warning("Unable to rewind SMUSH movie (z-lib copy handle failed)!");
		return false;
	}
	_fileDone = false;
	return true;
}

bool zlibFile::open(const char *filename) {
	char flags = 0;
	_inBuf = (char *)calloc(1, BUFFER_SIZE);

	if (_handle) {
		if (debugLevel == DEBUG_SMUSH || debugLevel == DEBUG_WARN || debugLevel == DEBUG_ALL)
			warning("zlibFile::open() File %s already opened", filename);
		return false;
	}

	if (filename == NULL || *filename == 0)
		return false;

	_handle = g_resourceloader->openNewStream(filename);
	if (!_handle) {
		if (debugLevel == DEBUG_SMUSH || debugLevel == DEBUG_WARN || debugLevel == DEBUG_ALL)
			warning("zlibFile::open() zlibFile %s not found", filename);
		return false;
	}

	// Read in the GZ header
	fread(_inBuf, 2, sizeof(char), _handle);				// Header
	fread(_inBuf, 1, sizeof(char), _handle);				// Method
	fread(_inBuf, 1, sizeof(char), _handle); flags = _inBuf[0];		// Flags
	fread(_inBuf, 6, sizeof(char), _handle);				// XFlags

	// Xtra & Comment
	if (((flags & 0x04) != 0) || ((flags & 0x10) != 0)) {
		if (debugLevel == DEBUG_SMUSH || debugLevel == DEBUG_ERROR || debugLevel == DEBUG_ALL) {
			error("zlibFile::open() Unsupported header flag");
		}
		return false;
	}

	if ((flags & 0x08) != 0) {					// Orig. Name
		do {
			fread(_inBuf, 1, sizeof(char), _handle);
		} while(_inBuf[0] != 0);
	}

	if ((flags & 0x02) != 0) // CRC
		fread(_inBuf, 2, sizeof(char), _handle);

	memset(_inBuf, 0, BUFFER_SIZE - 1); // Zero buffer (debug)
	_stream.zalloc = NULL;
	_stream.zfree = NULL;
	_stream.opaque = Z_NULL;

	if (inflateInit2(&_stream, -15) != Z_OK && (debugLevel == DEBUG_SMUSH || debugLevel == DEBUG_ERROR || debugLevel == DEBUG_ALL))
		error("zlibFile::open() inflateInit2 failed");

	_stream.next_in = NULL;
	_stream.next_out = NULL;
	_stream.avail_in = 0;
	_stream.avail_out = BUFFER_SIZE-1;

	return true;
}

void zlibFile::close() {
	if (_handle) {
		fclose(_handle);
		_handle = NULL;
	}

	if (_inBuf) {
 		free(_inBuf);
 		_inBuf = NULL;
	}
}

bool zlibFile::isOpen() {
	return _handle != NULL;
}

uint32 zlibFile::read(void *ptr, uint32 len) {
	int result = Z_OK;
	bool fileEOF = false;

	if (_handle == NULL) {
		if (debugLevel == DEBUG_SMUSH || debugLevel == DEBUG_ERROR || debugLevel == DEBUG_ALL)
			error("zlibFile::read() File is not open!");
		return 0;
	}

	if (len == 0)
		return 0;

	_stream.next_out = (Bytef *)ptr;
	_stream.avail_out = len;

	_fileDone = false;
	while (_stream.avail_out != 0) {
		if (_stream.avail_in == 0) {	// !eof
			_stream.avail_in = fread(_inBuf, 1, BUFFER_SIZE-1, _handle);
			if (_stream.avail_in == 0) {
				fileEOF = true;
				break;
			}
			_stream.next_in = (Byte *)_inBuf;
		}

		result = inflate(&_stream, Z_NO_FLUSH);
		if (result == Z_STREAM_END) {	// EOF
			// "Stream end" is zlib's way of saying that it's done after the current call,
			// so as long as no calls are made after we've received this message we're OK
			if (debugLevel == DEBUG_SMUSH || debugLevel == DEBUG_NORMAL || debugLevel == DEBUG_ALL)
				printf("zlibFile::read() Stream ended\n");
			_fileDone = true;
			break;
		}
		if (result == Z_DATA_ERROR) {
			if (debugLevel == DEBUG_SMUSH || debugLevel == DEBUG_WARN || debugLevel == DEBUG_ALL)
				warning("zlibFile::read() Decompression error");
			_fileDone = true;
			break;
		}
		if (result != Z_OK || fileEOF) {
			if (debugLevel == DEBUG_SMUSH || debugLevel == DEBUG_WARN || debugLevel == DEBUG_ALL)
				warning("zlibFile::read() Unknown decomp result: %d/%d\n", result, fileEOF);
			_fileDone = true;
			break;
		}
	}

	return (int)(len - _stream.avail_out);
}
 
byte zlibFile::readByte() {
	unsigned char c;

	read(&c, 1);
	return c;
}

uint16 zlibFile::readUint16LE() {
	uint16 a = readByte();
	uint16 b = readByte();
	return a | (b << 8);
}

uint32 zlibFile::readUint32LE() {
	uint32 a = readUint16LE();
	uint32 b = readUint16LE();
	return (b << 16) | a;
}

uint16 zlibFile::readUint16BE() {
	uint16 b = readByte();
	uint16 a = readByte();
	return a | (b << 8);
}

uint32 zlibFile::readUint32BE() {
	uint32 b = readUint16BE();
	uint32 a = readUint16BE();
	return (b << 16) | a;
}
