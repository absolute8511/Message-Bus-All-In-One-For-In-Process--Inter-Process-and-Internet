#ifndef CORE_XPARAM_H
#define CORE_XPARAM_H

#include "msgbus_interface.h"
#include "../3rd_tools/jsoncpp/json.h"
#include "CommonUtility.hpp"
#include <string>
#include <string.h>
#include <boost/shared_array.hpp>
#include <iostream>
#include <fstream>
#include <errno.h>
#include <vector>
#include <map>
#include <stdlib.h>
#include <inttypes.h>

using std::string;
using std::wstring;
using std::vector;
using std::map;

//using namespace json_spirit;
using namespace NetMsgBus;

namespace core {

class XParam
{
public:
    void put_Int(const std::string& key, int32_t value)
    {
        Json::Value tmp = Json::Value::Int(value);
        m_internal_obj[key] = tmp;
    }
    void put_UInt(const std::string& key, uint32_t value)
    {
        Json::Value tmp = Json::Value::UInt(value);
        m_internal_obj[key] = tmp;
    }
    void put_ULongLong(const std::string& key, uint64_t value)
    {
        char str_longlong[sizeof(value) * 2 + 1];
        memset(str_longlong, 0, sizeof(value) * 2 + 1);
        snprintf(str_longlong, sizeof(value)*2 + 1, "%"PRIx64"", value);
        put_Str(key, str_longlong);
    }
    void put_WStr(const std::string& key, const std::wstring& value)
    {
        m_internal_obj[key] = std::string(StrW2A(value, "UTF-8"));
    }
    // only support the string end with "\0", if you want char array ,should use put_CharArray instead. 
    void put_Str(const std::string& key, const std::string& value)
    {
        m_internal_obj[key] = value;
    }
    void put_Bool(const std::string& key, bool value)
    {
        m_internal_obj[key] = value;
    }
    void put_Real(const std::string& key, double value)
    {
        m_internal_obj[key] = value;
    }
    void put_CharArray(const std::string& key, const std::string& chararray)
    {
        Json::Value arr_value;
        for(size_t i = 0; i < chararray.size(); i++)
        {
            uint8_t onechar = chararray[i];
            arr_value.append((uint32_t)onechar);
        }
        put_Value(key, arr_value);
    }
    void put_XParam(const std::string& key, const XParam& param)
    {
        put_Value(key, param.get_InternalValue());
    }
    void put_XParamVector(const std::string& key, const vector<XParam>& params)
    {
        Json::Value paramarray;
        for(size_t i = 0; i < params.size(); i++)
            paramarray.append(params[i].get_InternalValue());
        put_Value(key, paramarray);
    }
    void put_XParamMap(const std::string& key, const map<std::string, XParam> params)
    {
        Json::Value paramobj;
        map<std::string, XParam>::const_iterator cit = params.begin();
        while(cit != params.end())
        {
            paramobj[cit->first] = cit->second.get_InternalValue();
            ++cit;
        }
        put_Value(key, paramobj); 
    }
    void put_Value(const std::string& key, const Json::Value& value)
    {
        m_internal_obj[key] = value;
    }
    void get_Int(const std::string& key, int32_t& value) const
    {
        Json::Value _null;
        Json::Value result = m_internal_obj.get(key, _null);
        if(result.isInt())
        {
            value = result.asInt();
        }
    }
    void get_Int(const std::string& key, int16_t& value) const
    {
        Json::Value _null;
        Json::Value result = m_internal_obj.get(key, _null);
        if(result.isInt())
        {
            value = (int16_t)result.asInt();
        }
    }
    void get_Int(const std::string& key, int8_t& value) const
    {
        Json::Value _null;
        Json::Value result = m_internal_obj.get(key, _null);
        if(result.isInt())
        {
            value = (int8_t)result.asInt();
        }
    }
    void get_UInt(const std::string& key, uint8_t& value) const
    {
        Json::Value _null;
        Json::Value result = m_internal_obj.get(key, _null);
        if(result.isUInt() || result.isInt())
        {
            value = (uint8_t)result.asUInt();
        }
    }
    void get_UInt(const std::string& key, uint16_t& value) const
    {
        Json::Value _null;
        Json::Value result = m_internal_obj.get(key, _null);
        if(result.isUInt() || result.isInt())
        {
            value = (uint16_t)result.asUInt();
        }
    }
    void get_UInt(const std::string& key, uint32_t& value) const
    {
        Json::Value _null;
        Json::Value result = m_internal_obj.get(key, _null);
        if(result.isUInt() || result.isInt())
        {
            value = result.asUInt();
        }
    }
    void get_ULongLong(const std::string& key, uint64_t& value) const
    {
        std::string str_longlong;
        get_Str(key, str_longlong);
        value = strtoull(str_longlong.c_str(), NULL, 16);
    }
    void get_WStr(const std::string& key, std::wstring& value) const
    {
        Json::Value _null;
        Json::Value result = m_internal_obj.get(key, _null);
        if(result.isString())
        {
            std::string tmp = result.asString();
            value = std::wstring(StrA2W(tmp, "UTF-8"));
        }
    }
    void get_Str(const std::string& key, std::string& value) const
    {
        Json::Value _null;
        Json::Value result = m_internal_obj.get(key, _null);
        if(result.isString())
        {
            value = result.asString();
        }
    }
    void get_Bool(const std::string& key, bool& value) const
    {
        Json::Value _null;
        Json::Value result = m_internal_obj.get(key, _null);
        if(result.isBool())
        {
            value = result.asBool();
        }
    }
    void get_Real(const std::string& key, double& value) const
    {
        Json::Value _null;
        Json::Value result = m_internal_obj.get(key, _null);
        if(result.isDouble())
        {
            value = result.asDouble();
        }
    }
    void get_CharArray(const std::string& key, string& chararray) const
    {
        Json::Value result;
        get_Value(key, result);
        if(result.isArray())
        {
            chararray.clear();
            chararray.reserve(result.size());
            for(size_t i = 0; i < result.size(); i++)
            {
                uint8_t onechar;
                onechar = (uint8_t)result[i].asUInt();
                chararray.push_back(onechar);
            }
        }
    }
    void get_XParam(const std::string& key, XParam& param) const
    {
        Json::Value result;
        get_Value(key, result);
        if(result.isObject())
        {
            param.m_internal_obj = result;
        }
    }
    void get_XParamVector(const std::string& key, vector<XParam>& params) const
    {
        Json::Value result;
        get_Value(key, result);
        if(result.isArray())
        {
            params.clear();
            for(size_t i = 0; i < result.size(); i++)
            {
                XParam oneparam;
                oneparam.m_internal_obj = result[i];
                params.push_back(oneparam);
            }
        }
    }
    void get_XParamMap(const std::string& key, map<string, XParam>& params) const
    {
        Json::Value result;
        get_Value(key, result);
        if(result.isObject())
        {
            params.clear();
            Json::Value::iterator it = result.begin();
            while(it != result.end())
            {
                XParam oneparam;
                oneparam.m_internal_obj = *it;
                std::string key = std::string(it.memberName());
                params[key] = oneparam; 
                ++it;
            }
        }
    }

    void get_Value(const std::string& key, Json::Value& value) const
    {
        Json::Value _null;
        value = m_internal_obj.get(key, _null);
    }
    const Json::Value& get_InternalValue() const
    {
        return m_internal_obj;
    } 
    bool FromMsgBusParam(MsgBusParam param)
    {
        std::string jsonstr(param.paramdata.get(), param.paramlen);
        return FromJsonStr(jsonstr);
    }
    MsgBusParam ToMsgBusParam() const
    {
        std::string jsonstr;
        jsonstr = ToJsonStr();

        boost::shared_array<char> data(new char[jsonstr.size()]);
        memcpy(data.get(), jsonstr.data(), jsonstr.size());
        MsgBusParam param(data, jsonstr.size());
        return param;
    }
    bool FromJsonStr(const std::string& jsonstr)
    {
        m_internal_obj.clear();
        Json::Reader reader;
        Json::Value root;
        if(reader.parse(jsonstr, root))
        {
            if(root.isObject())
            {
                m_internal_obj = root;
                return true;
            }
        }
        return false;
    }
    std::string ToJsonStr() const
    {
        Json::FastWriter writer;
        return writer.write(m_internal_obj);
    }
    bool FromJsonFile(const std::string& filepath)
    {
        std::fstream filestr; 
        filestr.open(filepath.c_str(), std::fstream::in);
        if(filestr.is_open())
        {
            Json::Reader reader;
            Json::Value root;
            bool result = reader.parse(filestr, root);
            if(result)
            {
                if(root.isObject())
                {
                    m_internal_obj = root;
                }
            }
            else
            {
                std::cout << reader.getFormatedErrorMessages() << std::endl;
            }
            filestr.close();
            return result;
        }
        perror("open json file for read failed.");
        printf("file:%s\n.", filepath.c_str());
        return false;
    }
    bool ToJsonFile(const std::string& filepath)
    {
        std::fstream filestr;
        filestr.open(filepath.c_str(), std::fstream::out|std::fstream::trunc);
        if(filestr.is_open())
        {
            Json::StyledStreamWriter writer;
            writer.write(filestr, m_internal_obj);
            filestr.close();
            return true;
        }
        perror("open json file for write failed.");
        printf("file:%s\n.", filepath.c_str());
        return false;
    }
    
private:
    Json::Value m_internal_obj;
};

}



#endif
