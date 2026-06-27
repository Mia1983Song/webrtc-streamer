/* ---------------------------------------------------------------------------
 * SPDX-License-Identifier: Unlicense
 *
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
 * software, either in source code form or as a compiled binary, for any purpose,
 * commercial or non-commercial, and by any means.
 *
 * For more information, please refer to <http://unlicense.org/>
 * -------------------------------------------------------------------------*/

#pragma once


#include "livevideosource.h"
#include "rtspconnectionclient.h"

class RTSPVideoCapturer : public LiveVideoSource<RTSPConnection>
{
	public:
		RTSPVideoCapturer(const std::string & uri, const std::map<std::string,std::string> & opts, std::unique_ptr<webrtc::VideoDecoderFactory>& videoDecoderFactory);
		virtual ~RTSPVideoCapturer();

		static RTSPVideoCapturer* Create(const std::string & url, const std::map<std::string, std::string> & opts, std::unique_ptr<webrtc::VideoDecoderFactory>& videoDecoderFactory) {
			return new RTSPVideoCapturer(url, opts, videoDecoderFactory);
		}
		
		// overide RTSPConnection::Callback
		virtual void    onConnectionTimeout(RTSPConnection& connection) override {
				// [FORK] Phase0: live555 logs timeouts via envir()->stderr only; surface to FileLogSink so
				// "connected then stalled" becomes visible (CC-878 freeze telemetry).
				RTC_LOG(LS_WARNING) << "RTSPVideoCapturer:onConnectionTimeout url:" << m_liveclient.getUrl();
				connection.start();
		}
		virtual void    onDataTimeout(RTSPConnection& connection) override {
				// [FORK] Phase0: no RTP received within timeout while the session was up => stream stall.
				RTC_LOG(LS_WARNING) << "RTSPVideoCapturer:onDataTimeout (no RTP within timeout) url:" << m_liveclient.getUrl();
				connection.start();
		}
		virtual void    onError(RTSPConnection& connection,const char* erro) override;
};


