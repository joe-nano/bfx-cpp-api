////////////////////////////////////////////////////////////////////////////////
//
// JSON utility routines for BitfinexAPI
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

// rapidjson
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/schema.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

// internal error
#include "error.hpp"

// std
#include <iostream>
#include <string>
#include <unordered_set>

// namespaces
using std::cerr;
using std::cout;
using std::endl;
using std::string;
using std::unordered_set;
namespace rj = rapidjson;


///////////////////////////////////////////////////////////////////////////////
// Routines
///////////////////////////////////////////////////////////////////////////////

namespace jsonutils
{
    // Helper class resolving remote schema for schema $ref operator
    class MyRemoteSchemaDocumentProvider: public rj::IRemoteSchemaDocumentProvider
    {
    public:
        
        MyRemoteSchemaDocumentProvider()
        {
            FILE *pFileIn = fopen("doc/definitions.json", "r"); // non-Windows use "r"
            char readBuffer[65536];
            rj::FileReadStream fReadStream(pFileIn, readBuffer, sizeof(readBuffer));
            rj::Document d;
            d.ParseStream(fReadStream);
            fclose(pFileIn);
            remoteSchemaDoc = new rj::SchemaDocument(d);
        };
        
        ~MyRemoteSchemaDocumentProvider()
        {
            delete remoteSchemaDoc;
        };
        
    private:
        
        rj::SchemaDocument *remoteSchemaDoc;
        
        virtual const rj::SchemaDocument*
        GetRemoteDocument(const char* uri, rj::SizeType length)
        {
            // Resolve the URI and return a pointer to that schema
            return remoteSchemaDoc;
        }
    };
    
    // SAX events helper struct for jsonStrToUset() routine
    struct jsonStrToUsetHandler:
    public rj::BaseReaderHandler<rj::UTF8<>, jsonStrToUsetHandler>
    {
        // Constructor
        jsonStrToUsetHandler():state_(State::kExpectArrayStart) {}
        
        // SAX events handlers
        bool StartArray() noexcept
        {
            switch (state_)
            {
                case State::kExpectArrayStart:
                    state_ = State::kExpectValueOrArrayEnd;
                    return true;
                default:
                    return false;
            }
        }
        
        bool String(const char *str, rj::SizeType length, bool)
        {
            switch (state_)
            {
                case State::kExpectValueOrArrayEnd:
                    handlerUSet_.emplace(str);
                    return true;
                default:
                    return false;
            }
        }
        
        bool EndArray(rj::SizeType) noexcept
        {
            return state_ == State::kExpectValueOrArrayEnd;
        }
        
        bool Default() { return false; } // All other events are invalid.
        
        // Handler attributes
        std::unordered_set<std::string> handlerUSet_; // output uSet
        enum class State // valid states
        {
            kExpectArrayStart,
            kExpectValueOrArrayEnd,
        } state_;
    };
    
    bfxERR jsonStrToUset(unordered_set<string> &uSet, const string &jsonStr)
    {
        // Create schema $ref resolver
        rj::Document sd;
        string schema = "{ \"$ref\": \"doc/definitions.json#/flatJsonSchema\" }";
        sd.Parse(schema.c_str());
        MyRemoteSchemaDocumentProvider provider;
        rj::SchemaDocument schemaDoc(sd, 0, 0, &provider);
        
        // Create SAX events handler which contains parsed uSet after successful
        // parsing
        jsonStrToUsetHandler handler;
        
        // Create schema validator
        rj::GenericSchemaValidator<rj::SchemaDocument, jsonStrToUsetHandler> validator(schemaDoc, handler);
        
        // Create reader
        rj::Reader reader;
        
        /// DEBUG
//        string mockJson = "{\"mid\":\"6581.55\",\"bid\":\"6581.5\",\"ask\":\"6581.6\",\"last_price\":\"6581.5\",\"low\":\"6333.2\",\"high\":\"6681.2\",\"volume\":\"28766.900098530004\",\"timestamp\":\"1530620498.4127066\"}";
        /// DEBUG
        
        // Create input JSON StringStream
        rj::StringStream ss(jsonStr.c_str());
//        rj::StringStream ss(mockJson.c_str()); // DEBUG
        
        // Parse and validate
        if (!reader.Parse(ss, validator))
        {
            // Input JSON is invalid according to the schema
            // Output diagnostic information
            cerr << "Error(offset " << reader.GetErrorOffset() << "): ";
            cerr << GetParseError_En(reader.GetParseErrorCode()) << endl;
            
            if (!validator.IsValid())
            {
                rj::StringBuffer sb;
                validator.GetInvalidSchemaPointer().StringifyUriFragment(sb);
                cerr << "Invalid schema: " << sb.GetString() << endl;
                cerr << "Invalid keyword: " << validator.GetInvalidSchemaKeyword() << endl;
                sb.Clear();
                validator.GetInvalidDocumentPointer().StringifyUriFragment(sb);
                cerr << "Invalid document: " << sb.GetString() << endl;
            }
            return bfxERR::jsonStrToUSetError;
        }
        else
        {
            uSet.swap(handler.handlerUSet_);
            return bfxERR::noError;
        }
    }
}
