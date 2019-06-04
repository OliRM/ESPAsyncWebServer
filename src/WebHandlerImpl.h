/*
  Asynchronous WebServer library for Espressif MCUs

  Copyright (c) 2016 Hristo Gochkov. All rights reserved.
  This file is part of the esp8266 core for Arduino environment.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#ifndef ASYNCWEBSERVERHANDLERIMPL_H_
#define ASYNCWEBSERVERHANDLERIMPL_H_


#include "stddef.h"
#include <time.h>

#include <ArduinoJson.h>

constexpr const char* JSON_MIMETYPE = "application/json";

class AsyncStaticWebHandler: public AsyncWebHandler {
   using File = fs::File;
   using FS = fs::FS;
  private:
    bool _getFile(AsyncWebServerRequest *request);
    bool _fileExists(AsyncWebServerRequest *request, const String& path);
    uint8_t _countBits(const uint8_t value) const;
  protected:
    FS _fs;
    String _uri;
    String _path;
    String _default_file;
    String _cache_control;
    String _last_modified;
    AwsTemplateProcessor _callback;
    bool _isDir;
    bool _gzipFirst;
    uint8_t _gzipStats;
  public:
    AsyncStaticWebHandler(const char* uri, FS& fs, const char* path, const char* cache_control);
    virtual bool canHandle(AsyncWebServerRequest *request) override final;
    virtual void handleRequest(AsyncWebServerRequest *request) override final;
    AsyncStaticWebHandler& setIsDir(bool isDir);
    AsyncStaticWebHandler& setDefaultFile(const char* filename);
    AsyncStaticWebHandler& setCacheControl(const char* cache_control);
    AsyncStaticWebHandler& setLastModified(const char* last_modified);
    AsyncStaticWebHandler& setLastModified(struct tm* last_modified);
  #ifdef ESP8266
    AsyncStaticWebHandler& setLastModified(time_t last_modified);
    AsyncStaticWebHandler& setLastModified(); //sets to current time. Make sure sntp is runing and time is updated
  #endif
    AsyncStaticWebHandler& setTemplateProcessor(AwsTemplateProcessor newCallback) {_callback = newCallback; return *this;}
};

class AsyncCallbackWebHandler: public AsyncWebHandler {
  private:
  protected:
    String _uri;
    WebRequestMethodComposite _method;
    ArRequestHandlerFunction _onRequest;
    ArUploadHandlerFunction _onUpload;
    ArBodyHandlerFunction _onBody;
  public:
    AsyncCallbackWebHandler() : _uri(), _method(HTTP_ANY), _onRequest(NULL), _onUpload(NULL), _onBody(NULL){}
    void setUri(const String& uri){ _uri = uri; }
    void setMethod(WebRequestMethodComposite method){ _method = method; }
    void onRequest(ArRequestHandlerFunction fn){ _onRequest = fn; }
    void onUpload(ArUploadHandlerFunction fn){ _onUpload = fn; }
    void onBody(ArBodyHandlerFunction fn){ _onBody = fn; }

    virtual bool canHandle(AsyncWebServerRequest *request) override final{

      if(!_onRequest)
        return false;

      if(!(_method & request->method()))
        return false;

      if(_uri.length() && (_uri != request->url() && !request->url().startsWith(_uri+"/")))
        return false;

      request->addInterestingHeader("ANY");
      return true;
    }
  
    virtual void handleRequest(AsyncWebServerRequest *request) override final {
      if(_onRequest)
        _onRequest(request);
      else
        request->send(500);
    }
    virtual void handleUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) override final {
      if(_onUpload)
        _onUpload(request, filename, index, data, len, final);
    }
    virtual void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) override final {
      if(_onBody)
        _onBody(request, data, len, index, total);
    }
    virtual bool isRequestHandlerTrivial() override final {return _onRequest ? false : true;}
};

class ChunkPrint : public Print {
  private:
    uint8_t* _destination;
    size_t _to_skip;
    size_t _to_write;
    size_t _pos;
  public:
    ChunkPrint(uint8_t* destination, size_t from, size_t len)
      : _destination(destination), _to_skip(from), _to_write(len), _pos{0} {}
    virtual ~ChunkPrint(){}
    size_t write(uint8_t c){
      if (_to_skip > 0) {
        _to_skip--;
        return 1;
      } else if (_to_write > 0) {
        _to_write--;
        _destination[_pos++] = c;
        return 1;
      }
      return 0;
    }
};

class AsyncJsonResponse: public AsyncAbstractResponse {
  private:
    DynamicJsonBuffer _jsonBuffer;
    JsonVariant _root;
    bool _isValid;
  public:
    AsyncJsonResponse(bool isArray=false): _isValid{false} {
      _code = 200;
      _contentType = JSON_MIMETYPE;
      if(isArray)
        _root = _jsonBuffer.createArray();
      else
        _root = _jsonBuffer.createObject();
    }
    ~AsyncJsonResponse() {}
    JsonVariant & getRoot() { return _root; }
    bool _sourceValid() const { return _isValid; }
    size_t setLength() {
      _contentLength = _root.measureLength();
      if (_contentLength) { _isValid = true; }
      return _contentLength;
    }

   size_t getSize() { return _jsonBuffer.size(); }

    size_t _fillBuffer(uint8_t *data, size_t len){
      ChunkPrint dest(data, _sentLength, len);
      _root.printTo( dest ) ;
      return len;
    }
};

class AsyncCallbackJsonWebHandler: public AsyncWebHandler {
private:
protected:
  const String _uri;
  WebRequestMethodComposite _method;
  ArJsonRequestHandlerFunction _onRequest;
  int _contentLength;
  int _maxContentLength;
public:
  AsyncCallbackJsonWebHandler(const String& uri, ArJsonRequestHandlerFunction onRequest) : _uri(uri), _method(HTTP_POST|HTTP_PUT|HTTP_PATCH), _onRequest(onRequest), _maxContentLength(16384) {}
  void setMethod(WebRequestMethodComposite method){ _method = method; }
  void setMaxContentLength(int maxContentLength){ _maxContentLength = maxContentLength; }
  void onRequest(ArJsonRequestHandlerFunction fn){ _onRequest = fn; }

  virtual bool canHandle(AsyncWebServerRequest *request) override final{
    if(!_onRequest)
      return false;

    if(!(_method & request->method()))
      return false;

    if(_uri.length() && (_uri != request->url() && !request->url().startsWith(_uri+"/")))
      return false;

    if (!request->contentType().equalsIgnoreCase(JSON_MIMETYPE))
      return false;

    request->addInterestingHeader("ANY");
    return true;
  }

  virtual void handleRequest(AsyncWebServerRequest *request) override final {
    if(_onRequest) {
      if (request->_tempObject != NULL) {
        DynamicJsonBuffer jsonBuffer;
        JsonVariant json = jsonBuffer.parse((uint8_t*)(request->_tempObject));
        if (json.success()) {
          _onRequest(request, json);
          return;
        }
      }
      request->send(_contentLength > _maxContentLength ? 413 : 400);
    } else {
      request->send(500);
    }
  }
  virtual void handleUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) override final {
  }
  virtual void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) override final {
    if (_onRequest) {
      _contentLength = total;
      if (total > 0 && request->_tempObject == NULL && total < _maxContentLength) {
        request->_tempObject = malloc(total);
      }
      if (request->_tempObject != NULL) {
        memcpy((uint8_t*)(request->_tempObject) + index, data, len);
      }
    }
  }
  virtual bool isRequestHandlerTrivial() override final {return _onRequest ? false : true;}
};

#endif /* ASYNCWEBSERVERHANDLERIMPL_H_ */
