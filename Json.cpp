#include "Json.h"
#include <charconv>
#include <list>

constexpr size_t DOUBLE_MAX = 15;  //0..14 + point(1)
constexpr size_t NEGATIVE_DOUBLE_MAX = DOUBLE_MAX + 1;
constexpr size_t INTEGER_MAX = 18; //0..18
constexpr size_t NEGATIVE_INTEGER_MAX = INTEGER_MAX + 1;

static inline bool isControlCode(unsigned char value){ return (value <= 8 || (value >= 14 && value <= 31) || value == 127); }

//----------------------------------------------------------------

JsonStringViewBufferReader::JsonStringViewBufferReader(std::string_view json):JsonBufferReader(), _json(json){}

bool JsonStringViewBufferReader::next()
{
    pos++;
    if(static_cast<std::size_t>(pos) == _json.size()) return false;
    return true;
}

unsigned char JsonStringViewBufferReader::value(){ return (static_cast<std::size_t>(pos) == _json.size()) ? 0 : _json[pos]; }

std::size_t JsonStringViewBufferReader::offset(){ return static_cast<std::size_t>(pos); }

//---------------

JsonFileBufferReader::JsonFileBufferReader(){}

bool JsonFileBufferReader::open(const std::string & fileName)
{
    c = 0;
    pos = 0;
    if(stream.is_open()) stream.close();
    stream.open(fileName);
    is_open = stream.is_open();
    return is_open;
}

bool JsonFileBufferReader::isOpen(){ return is_open; }

bool JsonFileBufferReader::next()
{
    if(stream.get(c))
    {
       pos++;
       return true;
    }

    return false;
}

unsigned char JsonFileBufferReader::value(){ return c; }

std::size_t JsonFileBufferReader::offset(){ return pos; }

//----------------------------------------------------------------

static const char * const ControlCharacterDetectionMsg = "Control character detection, offset: ",
                  * const InvalidNumberMsg = "Invalid number, offset: ",
                  * const ALotPointMsg = "A lot or an incorrect numeric point, offset: ",
                  * const NumberRangeMsg = "Number out of range, offset: ",
                  * const NumberOutOfArrayMsg = "Number out of array limit, offset: ",
                  * const StringOutOfArrayMsg = "String out of array limit, offset: ",
                  * const ValueOutOfArrayMsg = "Value out of array limit, offset: ",
                  * const InvalidValueMsg = "Invalid value, offset: ",
                  * const InvalidEntryCharacterMsg = "Invalid entry character '",
                  * const InvalidObjectKeyMsg = "Invalid starting symbol of the object key or the end of an object '",
                  * const InvalidObjectKeyValueMsg = "Invalid object key-value separator character '",
                  * const InvalidSeparatorObjectMsg = "Invalid pair separator or end of object symbol, offset: ",
                  * const InvalidSeparatorArrayMsg = "Invalid value separator or end of array symbol, offset: ",
                  * const UnexpectedEndMsg = "Unexpected end of json stream",
                  * const InvalidSpecialCharMsg = "Invalid special character in string '\\";

static std::string makeError(const char * msg, JsonBufferReader & buffer)
{
    return  msg + std::to_string(buffer.offset());
}

static std::string makeError(const char * msg, unsigned char ch, JsonBufferReader & buffer)
{
    return std::string(msg) + static_cast<const char>(ch) + "', offset: " + std::to_string(buffer.offset());
}

//----------------------------------------------------------------

enum class JsonReaderType : unsigned char
{
     Object = 0,
     ObjectKey,
     ObjectValue,
     ObjectNextPair,
     ObjectNextKey,

     Array,
     ArrayNext,
     ArrayNextValue
};

static bool readyString(std::string & temp, JsonBufferReader & buffer, std::string & error)
{
    bool exit = false, special = false;

    while(buffer.next())
    {
          const unsigned char ch = buffer.value();

          if(isControlCode(ch))
          {
             error =  makeError(ControlCharacterDetectionMsg, buffer);
             return false;
          }

          if(!special && ch == '"')
          {
             exit = true;
             break;
          }

          if(!special && ch == '\\')
          {
             special = true;
             continue;
          }

          if(special)
          {
             switch (ch)
             {
                case '"':
                case '\\':
                case '/': temp.push_back(ch);
                break;
                case 'b': temp.push_back('\b');
                break;
                case 'f': temp.push_back('\f');
                break;
                case 'n': temp.push_back('\n');
                break;
                case 'r': temp.push_back('\r');
                break;
                case 't': temp.push_back('\t');
                break;
                case 'u':
                {
                     temp.push_back('\\');
                     temp.push_back(ch);
                }
                break;
                default:
                {
                     error =  makeError(InvalidSpecialCharMsg, ch, buffer);
                     return false;
                }
             }

             special = false;
             continue;
          }

          temp.push_back(ch);
    }

    if(!exit)
    {
       error =  makeError(StringOutOfArrayMsg, buffer);
       return false;
    }

    return true;
}

static inline bool readyObjectKey(JsonSAXReader * self, JsonBufferReader & buffer, std::string & error)
{
    std::string temp;
    if(!readyString(temp, buffer, error)) return false;
    self->ObjectKey(temp);
    return true;
}

static inline bool readyStringValue(JsonSAXReader * self, JsonBufferReader & buffer, std::string & error)
{
    std::string temp;
    if(!readyString(temp, buffer, error)) return false;
    self->Value(temp);
    return true;
}

static inline bool readyNumber(const unsigned char digit, std::stack<JsonReaderType> & depth, JsonSAXReader * self, JsonBufferReader & buffer, std::string & error)
{
    enum End
    {
         None = 0,
         Object,
         Array,
         Separator
    };

    End end = None;
    int points = 0;
    bool neg = (digit == '-'), exit = false;

    std::string temp;
    temp.push_back(digit);

    unsigned char ch;
    for(std::size_t i = 0; buffer.next(); i++)
    {
        ch = buffer.value();

        if(isControlCode(ch))
        {
           error =  makeError(ControlCharacterDetectionMsg, buffer);
           return false;
        }

        //-----------------------------------------------------------------------

        if(std::isspace(ch))
        {
           exit = true;
           break;
        }

        if(ch == ',')
        {
           if(depth.top() < JsonReaderType::Array) depth.top() = JsonReaderType::ObjectNextKey;
           else depth.top() = JsonReaderType::ArrayNextValue;

           end = Separator;
           exit = true;
           break;
        }

        if(ch == '}')
        {
           if(depth.top() > JsonReaderType::ObjectNextKey)
           {
              error =  makeError(InvalidSeparatorArrayMsg, buffer);
              return false;
           }

           depth.pop();
           end = Object;
           exit = true;
           break;
        }

        if(ch == ']')
        {
           if(depth.top() < JsonReaderType::Array)
           {
              error =  makeError(InvalidSeparatorObjectMsg, buffer);
              return false;
           }

           depth.pop();
           end = Array;
           exit = true;
           break;
        }

        //-----------------------------------------------------------------------

        if(neg)
        {
           if((points > 0 && i == NEGATIVE_DOUBLE_MAX) || (points == 0 && i == NEGATIVE_INTEGER_MAX))
           {
              exit = true;
              break;
           }
        }
        else if((points > 0 && i == DOUBLE_MAX) || (points == 0 && i == INTEGER_MAX))
        {
           exit = true;
           break;
        }

        //-----------------------------------------------------------------------

        if(ch == '.')
        {
           if(points == 1 || (neg && i == 0))
           {
              error =  makeError(ALotPointMsg, buffer);
              return false;
           }

           temp.push_back(ch);
           points++;
           continue;
        }

        if(std::isdigit(ch) == 0)
        {
           error =  makeError(InvalidNumberMsg, buffer);
           return false;
        }

        temp.push_back(ch);
    }

    if(!exit)
    {
       error =  makeError(NumberOutOfArrayMsg, buffer);
       return false;
    }

    if(end == None && std::isspace(ch) == 0)
    {
       error =  makeError(InvalidValueMsg, buffer);
       return false;
    }

    if(temp.back() == '.')
    {
       error =  makeError(ALotPointMsg, buffer);
       return false;
    }

    if(points == 1)
    {
       double value = std::stod(temp, nullptr);

       if(errno == ERANGE)
       {
          error =  makeError(NumberRangeMsg, buffer);
          return false;
       }

       self->Value(value);
    }
    else if(points == 0)
    {
       long long value;
       std::string_view view(temp.begin(), temp.end());
       auto [ptr, ec] { std::from_chars(view.begin(), view.end(), value) };

       if(ec != std::errc())
       {
          error =  makeError(NumberRangeMsg, buffer);
          return false;
       }

       self->Value(value);
    }

    if(end == Object) self->ObjectEnd();
    else if(end == Array) self->ArrayEnd();

    return true;
}

static inline bool readyValue(std::string_view value, JsonBufferReader & buffer, std::string & error)
{
    std::size_t i = 0;
    while(i < value.size() && buffer.next())
    {
          unsigned char ch = buffer.value();

          if(isControlCode(ch))
          {
           error =  makeError(ControlCharacterDetectionMsg, buffer);
           return false;
          }

          if(ch != value[i])
          {
             error =  makeError(InvalidValueMsg, buffer);
             return false;
          }

          i++;
    }

    if(i != value.size())
    {
       error =  makeError(ValueOutOfArrayMsg, buffer);
       return false;
    }

    return true;
}

static bool ready(const unsigned char ch,
                  std::stack<JsonReaderType> & depth,
                  JsonSAXReader * self,
                  JsonBufferReader & buffer,
                  std::string & error)
{
    if(ch == '{')
    {
       depth.push(JsonReaderType::Object);
       self->ObjectBegin();
    }
    else if(ch == '[')
    {
       depth.push(JsonReaderType::Array);
       self->ArrayBegin();
    }
    else if(ch == '"')
    {
       if(!readyStringValue(self, buffer, error)) return false;
    }
    else if(ch == '-' || std::isdigit(ch) != 0)
    {
       if(!readyNumber(ch, depth, self, buffer, error)) return false;
    }
    else if(ch == 't')
    {
       if(!readyValue("rue", buffer, error)) return false;
       self->Value(true);
    }
    else if(ch == 'f')
    {
       if(!readyValue("alse", buffer, error)) return false;
       self->Value(false);
    }
    else if(ch == 'n')
    {
       if(!readyValue("ull", buffer, error)) return false;
       self->Null();
    }
    else
    {
       error =  makeError(InvalidValueMsg, buffer);
       return false;
    }

    return true;
}

//----------------------------------------------------------------

void JsonSAXReader::stopParse(){ stop = true; }

JsonSAXReader::JsonSAXReader(){}
JsonSAXReader::~JsonSAXReader(){}

std::string JsonSAXReader::error() const { return std::move(_error); }

bool JsonSAXReader::parse(JsonBufferReader & buffer, Operation operation) //pop top
{
    stop = false;
    std::stack<JsonReaderType> depth;

    while(buffer.next())
    {
        const unsigned char ch = buffer.value();

        if(std::isspace(ch) != 0) continue;

        if(isControlCode(ch))
        {
           _error =  makeError(ControlCharacterDetectionMsg, buffer);
           return false;
        }

        if(depth.empty())
        {
           if(ch == '{')
           {
              JsonBegin();
              depth.push(JsonReaderType::Object);
              ObjectBegin();
           }
           else if(ch == '[')
           {
              JsonBegin();
              depth.push(JsonReaderType::Array);
              ArrayBegin();
           }
           else
           {
              _error =  makeError(InvalidEntryCharacterMsg, ch, buffer);
              return false;
           }
        }
        else if(depth.top() == JsonReaderType::Object)
        {
           if(ch == '"')
           {
              if(!readyObjectKey(this, buffer, _error)) return false;
              depth.top() = JsonReaderType::ObjectKey;
           }
           else if(ch == '}')
           {
              depth.pop();
              ObjectEnd();
           }
           else
           {
              _error =  makeError(InvalidObjectKeyMsg, ch, buffer);
              return false;
           }
        }
        else if(depth.top() == JsonReaderType::ObjectKey)
        {
           if(ch == ':')
           {
              depth.top() = JsonReaderType::ObjectValue;
           }
           else
           {
              _error =  makeError(InvalidObjectKeyValueMsg, ch, buffer);
              return false;
           }
        }
        else if(depth.top() == JsonReaderType::ObjectValue)
        {
           depth.top() = JsonReaderType::ObjectNextPair;
           if(!ready(ch, depth, this, buffer, _error)) return false;
        }
        else if(depth.top() == JsonReaderType::ObjectNextPair)
        {
           if(ch == ',')
           {
              depth.top() = JsonReaderType::ObjectNextKey;
           }
           else if(ch == '}')
           {
              depth.pop();
              ObjectEnd();
           }
           else
           {
              _error =  makeError(InvalidSeparatorObjectMsg, buffer);
              return false;
           }
        }
        else if(depth.top() == JsonReaderType::ObjectNextKey)
        {
           if(ch == '"')
           {
              if(!readyObjectKey(this, buffer, _error)) return false;
              depth.top() = JsonReaderType::ObjectKey;
           }
           else
           {
              _error = makeError(InvalidObjectKeyMsg, ch, buffer);
              return false;
           }
        }
        else if(depth.top() == JsonReaderType::Array)
        {
           if(ch == ']')
           {
              depth.pop();
              ArrayEnd();
           }
           else
           {
              depth.top() = JsonReaderType::ArrayNext;
              if(!ready(ch, depth, this, buffer, _error)) return false;
           }
        }
        else if(depth.top() == JsonReaderType::ArrayNext)
        {
           if(ch == ',')
           {
              depth.top() = JsonReaderType::ArrayNextValue;
           }
           else if(ch == ']')
           {
              depth.pop();
              ArrayEnd();
           }
           else
           {
              _error =  makeError(InvalidSeparatorArrayMsg, buffer);
              return false;
           }
        }
        else if(depth.top() == JsonReaderType::ArrayNextValue)
        {
           depth.top() = JsonReaderType::ArrayNext;
           if(!ready(ch, depth, this, buffer, _error)) return false;
        }

        if(depth.empty())
        {
           JsonEnd();
           if(operation == Single) break;
           if(stop) break;
        }
    }

    if(!depth.empty())
    {
       _error = UnexpectedEndMsg;
       return false;
    }

    return true;
}

//----------------------------------------------------------------

JsonValue::Object::Object(){}
JsonValue::Object::Object(const Map & map){ *this->map = map; }
JsonValue::Object JsonValue::Object::copy() const
{
   Object ret;
   *ret.map = *map;
   return ret;
}

std::size_t JsonValue::Object::count() const { return map->size(); }
bool JsonValue::Object::contains(const std::string & key) const { return map->contains(key); }
JsonValue JsonValue::Object::value(const std::string & key) const { return (map->contains(key)) ? map->operator[](key) : JsonValue(); }
void JsonValue::Object::insert(const std::string & key, const JsonValue & value) const { map->insert({key, value}); }
void JsonValue::Object::remove(const std::string & key){ map->erase(key); }
void JsonValue::Object::clear(){ map->clear(); }
JsonValue & JsonValue::Object::operator [](const std::string & key) const { return map->operator[](key); }
const JsonValue::Object::Map & JsonValue::Object::getMap() const { return *map; }
JsonValue::Object::Map & JsonValue::Object::getMap(){ return *map; }

JsonValue::Object::operator const Map &() const { return *map; }
JsonValue::Object::operator Map &(){ return *map; }

void JsonValue::Object::setMap(const Map & map){ *this->map = map; }
JsonValue::Object & JsonValue::Object::operator = (const Map & map)
{
   *this->map = map;
   return *this;
}

//----------------------

JsonValue::Array::Array(){}
JsonValue::Array::Array(const Vector & array){ *this->array = array; }
JsonValue::Array JsonValue::Array::copy() const
{
   Array ret;
   *ret.array = *array;
   return ret;
}

std::size_t JsonValue::Array::count() const { return array->size(); }
JsonValue & JsonValue::Array::at(std::size_t index) const { return array->at(index); }
void JsonValue::Array::append(const JsonValue & value){ array->push_back(value); }
void JsonValue::Array::clear(){ array->clear(); }
JsonValue & JsonValue::Array::operator[](std::size_t index) const { return array->at(index); }
const JsonValue::Array::Vector & JsonValue::Array::getVector() const { return *array; }
JsonValue::Array::Vector & JsonValue::Array::getVector(){ return *array; }

JsonValue::Array::operator const Vector &() const{ return *array; }
JsonValue::Array::operator Vector &() { return *array; }

void JsonValue::Array::setVector(const Vector & vector){ *array = vector; }
JsonValue::Array & JsonValue::Array::operator = (const Vector & vector)
{
   *array = vector;
   return *this;
}

//----------------------

JsonValue::JsonValue(){}
JsonValue::JsonValue(const Object & object){ *value = object; }
JsonValue::JsonValue(const Array & array){ *value = array; }
JsonValue::JsonValue(char c){ setString(c); }
JsonValue::JsonValue(const char * string){ setString(string); }
JsonValue::JsonValue(std::string_view string){ setString(string); }
JsonValue::JsonValue(const std::string & string){ *value = string; }
JsonValue::JsonValue(float val){ setDouble(val); }
JsonValue::JsonValue(double val){ *value = val; }
JsonValue::JsonValue(unsigned char val){ setLongLong(val); }
JsonValue::JsonValue(short val){ setLongLong(val); }
JsonValue::JsonValue(unsigned short val){ setLongLong(val); }
JsonValue::JsonValue(int val){ setLongLong(val); }
JsonValue::JsonValue(unsigned int val){ setLongLong(val); }
JsonValue::JsonValue(long long val){ *value = val; }
JsonValue::JsonValue(bool val){ *value = val; }
JsonValue::JsonValue(std::nullptr_t){ *value = std::nullptr_t(); }

JsonValue JsonValue::copy() const
{
   JsonValue ret;
   *ret.value = *value;
   return ret;
}

JsonType JsonValue::type() const { return static_cast<JsonType>(value->index()); }
bool JsonValue::isEmpty() const { return (value->index() == 0); }

const JsonValue::Value & JsonValue::getValue() const { return *value; }
JsonValue::Value & JsonValue::getValue(){ return *value; }
JsonValue::operator const Value &() const { return *value; }
JsonValue::operator Value &() { return *value; }
void JsonValue::setValue(const Value & value) { *this->value = value; }
JsonValue & JsonValue::operator = (const Value & value)
{
   *this->value = value;
   return *this;
}

JsonValue::Object JsonValue::getObject() const { return (value->index() == 1) ? std::get<1>(*value.get()) : Object(); }
void JsonValue::setObject(const Object & object){ *value = object; }
JsonValue::operator Object() const { return getObject(); }
JsonValue & JsonValue::operator = (const Object & object)
{
   *value = object;
   return *this;
}
JsonValue::Array JsonValue::getArray() const { return (value->index() == 2) ? std::get<2>(*value.get()) : Array(); }
void JsonValue::setArray(const Array & array){ *value = array; }
JsonValue::operator Array() const { return getArray(); }
JsonValue & JsonValue::operator = (const Array & array)
{
   *value = array;
   return *this;
}

std::string JsonValue::getString() const
{
   switch(value->index())
   {
    case 1:
    case 2: return JsonWriter().write(*this, true);
    case 3: return std::get<3>(*value.get());
    case 4:
    {
       std::array<char, 18> data;
       auto [ptr, ec] = std::to_chars(data.data(), data.data() + data.size(), std::get<4>(*value.get()));
       if(ec != std::errc()) return std::string();
       return std::string(data.data(), ptr);
    }
    case 5:
    {
       std::array<char, 20> data;
       auto [ptr, ec] = std::to_chars(data.data(), data.data() + data.size(), std::get<5>(*value.get()));
       if(ec != std::errc()) return std::string();
       return std::string(data.data(), ptr);
    }
    case 6: return (std::get<6>(*value.get())) ? "true" : "false";
    case 7: return "null";
    default: return std::string();
   }
}

void JsonValue::setString(char c){ *value = std::string(&c, 1); }
void JsonValue::setString(const char * string){ *value = std::string(string); }
void JsonValue::setString(std::string_view string){ *value = std::string(string); }
void JsonValue::setString(const std::string & string){ *value = string; }
JsonValue::operator std::string() const { return getString(); }
JsonValue & JsonValue::operator = (char c)
{
   setString(c);
   return *this;
}
JsonValue & JsonValue::operator = (const char * string)
{
   setString(string);
   return *this;
}
JsonValue & JsonValue::operator = (std::string_view string)
{
   setString(string);
   return *this;
}
JsonValue & JsonValue::operator = (const std::string & string)
{
   *value = string;
   return *this;
}
double JsonValue::getDouble() const { return (value->index() == 4) ? std::get<4>(*value.get()) : 0.0; }
void JsonValue::setDouble(float val){  *value = static_cast<double>(val);  }
void JsonValue::setDouble(double val){ *value = val; }
JsonValue::operator double() const { return getDouble(); }
JsonValue & JsonValue::operator = (float val)
{
   setDouble(val);
   return *this;
}
JsonValue & JsonValue::operator = (double val)
{
   *value = val;
   return *this;
}
long long JsonValue::getLongLong() const { return (value->index() == 5) ? std::get<5>(*value.get()) : 0; }
void JsonValue::setLongLong(unsigned char val){ *value = static_cast<long long>(val); }
void JsonValue::setLongLong(short val){ *value = static_cast<long long>(val); }
void JsonValue::setLongLong(unsigned short val){ *value = static_cast<long long>(val); }
void JsonValue::setLongLong(int val){ *value = static_cast<long long>(val); }
void JsonValue::setLongLong(unsigned int val){ *value = static_cast<long long>(val); }
void JsonValue::setLongLong(long long val){ *value = val; }
JsonValue::operator long long() const { return getLongLong(); }
JsonValue & JsonValue::operator = (unsigned char val)
{
   setLongLong(val);
   return *this;
}
JsonValue & JsonValue::operator = (short val)
{
   setLongLong(val);
   return *this;
}
JsonValue & JsonValue::operator = (unsigned short val)
{
   setLongLong(val);
   return *this;
}
JsonValue & JsonValue::operator = (int val)
{
   setLongLong(val);
   return *this;
}
JsonValue & JsonValue::operator = (unsigned int val)
{
   setLongLong(val);
   return *this;
}
JsonValue & JsonValue::operator = (long long val)
{
   *value = val;
   return *this;
}
bool JsonValue::getBool() const { return (value->index() == 6) ? std::get<6>(*value.get()) : false; }
void JsonValue::setBool(bool val){ *value = val; }
JsonValue::operator bool() const { return getBool(); }
JsonValue & JsonValue::operator = (bool val)
{
   *value = val;
   return *this;
}
bool JsonValue::getNull() const { return (value->index() == 7) ? true : false; }
void JsonValue::setNull(){ *value = std::nullptr_t(); }
JsonValue & JsonValue::operator = (std::nullptr_t)
{
   *value = std::nullptr_t();
   return *this;
}

//----------------------------------------------------------------

void JsonReader::insertValue(JsonValue & value)
{
    if(!key.empty())
    {
       constexpr int index = static_cast<int>(JsonType::Object);
       JsonValue::Object obj = std::get<index>(*stack.top());
       obj.map->insert({std::move(key), value});
    }
    else
    {
       constexpr int index = static_cast<int>(JsonType::Array);
       JsonValue::Array array = std::get<index>(*stack.top());
       array.array->push_back(value);
    }
}

JsonReader::JsonReader(){}

bool JsonReader::parse(JsonBufferReader & buffer, const std::function<bool(JsonValue &)> & resultCallback, Operation operation)
{
    if(!resultCallback) return false;
    callback = resultCallback;

    if(!JsonSAXReader::parse(buffer, operation))
    {
       while(!stack.empty()) stack.pop();
       root = JsonValue();
       key.clear();
       return false;
    }

    return true;
}

bool JsonReader::parse(std::string_view json, const std::function<bool (JsonValue &)> &resultCallback, Operation operation)
{
    JsonStringViewBufferReader buffer(json);
    return parse(buffer, resultCallback, operation);
}

JsonValue JsonReader::parse(JsonBufferReader & buffer)
{
    JsonValue ret;
    parse(buffer, [&ret](JsonValue & value)
    {
       ret = value;
       return true;
    });
    return ret;
}

JsonValue JsonReader::parse(std::string_view json)
{
    JsonValue ret;
    parse(json, [&ret](JsonValue & value)
    {
       ret = value;
       return true;
    });
    return ret;
}

bool JsonReader::parseFromFile(const std::string & fileName, const std::function<bool (JsonValue &)> &resultCallback, Operation operation)
{
    JsonFileBufferReader buffer;
    if(!buffer.open(fileName) || !parse(buffer, resultCallback, operation)) return false;
    return true;
}

JsonValue JsonReader::parseFromFile(const std::string & fileName)
{
    JsonValue ret;
    parseFromFile(fileName, [&ret](JsonValue & value)
    {
       ret = value;
       return true;
    });
    return ret;
}

void JsonReader::JsonBegin()
{
    while(!stack.empty()) stack.pop();
}

void JsonReader::JsonEnd()
{
    while(!stack.empty()) stack.pop();
    if(!callback(root)) stopParse();
}

void JsonReader::ObjectBegin()
{
    JsonValue value;
    *value.value = JsonValue::Object();

    if(stack.empty())
    {
       root = value;
       stack.push(std::move(value.value));
       return;
    }

    insertValue(value);
    stack.push(std::move(value.value));
}

void JsonReader::ObjectKey(const std::string & key){ this->key = std::move(key); }

void JsonReader::ObjectEnd(){ stack.pop(); }

void JsonReader::ArrayBegin()
{
    JsonValue value;
    *value.value = JsonValue::Array();

    if(stack.empty())
    {
       root = value;
       stack.push(std::move(value.value));
       return;
    }

    insertValue(value);
    stack.push(std::move(value.value));
}

void JsonReader::ArrayEnd(){ stack.pop(); }

void JsonReader::Value(const std::string & value)
{
    JsonValue val;
    *val.value = value;
    insertValue(val);
}

void JsonReader::Value(double value)
{
    JsonValue val;
    *val.value = value;
    insertValue(val);
}

void JsonReader::Value(long long value)
{
    JsonValue val;
    *val.value = value;
    insertValue(val);
}

void JsonReader::Value(bool value)
{
    JsonValue val;
    *val.value = value;
    insertValue(val);
}

void JsonReader::Null()
{
    JsonValue val;
    *val.value = nullptr;
    insertValue(val);
}

//----------------------------------------------------------------

JsonStringBufferWriter::JsonStringBufferWriter(){}

bool JsonStringBufferWriter::write(unsigned char ch)
{
    count++;
    json.push_back(ch);
    return true;
}

std::size_t JsonStringBufferWriter::writeCount(){ return count; }

const std::string & JsonStringBufferWriter::result() const { return json; }

//--------------

JsonFileBufferWriter::JsonFileBufferWriter(){}

bool JsonFileBufferWriter::open(const std::string &fileName)
{
    count = 0;
    if(stream.is_open()) stream.close();
    stream.open(fileName);
    is_open = stream.is_open();
    return is_open;
}

bool JsonFileBufferWriter::isOpen(){ return is_open; }

bool JsonFileBufferWriter::write(unsigned char ch)
{
    if(!is_open) return false;
    stream << ch;
    count++;
    return true;
}

std::size_t JsonFileBufferWriter::writeCount(){ return count; };

//----------------------------------------------------------------

static const char * const InvalidBuffer = "Invalid buffer",
                  * const InvalidOperation = "Invalid operation",
                  * const ErrorConvDouble = "Error converting double to string",
                  * const ErrorConvLongLong = "Error converting long long to string",
                  * const ControlCharacterDetect = "Control character detection",
                  * const BufferEnding = "Buffer ending";

bool JsonSAXWriter::checkBuffer()
{
    if(buffer == nullptr)
    {
       _error = InvalidBuffer;
       return false;
    }

    return true;
}

bool JsonSAXWriter::writeChar(unsigned char ch)
{
    if(isControlCode(ch))
    {
       _error = ControlCharacterDetect;
       return false;
    }

    if(!buffer->write(ch))
    {
       _error = BufferEnding;
       return false;
    }

    return true;
}

bool JsonSAXWriter::writeSpace(int count)
{
    for(int i = 0; i < count; i++){ if(!writeChar(' ')) return false; }
    return true;
}

bool JsonSAXWriter::checkCorrectValue()
{
    if(!stack.empty() && (stack.top() == Сondition::Object || stack.top() == Сondition::ObjectNextPair))
    {
       _error = InvalidOperation;
       return false;
    }

    if(stack.top() == Сondition::ArrayNextValue)
    {
       if(!writeChar(',') || (beautiful && (!writeChar('\n') || !writeSpace(2 * stack.size())))) return false;
    }
    else if(stack.top() == Сondition::ObjectKey) stack.top() = Сondition::ObjectNextPair;
    else if(stack.top() ==  Сondition::Array)
    {
       if(beautiful && !writeSpace(2 * stack.size())) return false;
       stack.top() = Сondition::ArrayNextValue;
    }

    return true;
}

bool JsonSAXWriter::containerEnd()
{
    if(!stack.empty())
    {
       if(stack.top() == Сondition::ArrayNextValue)
       {
          if(!writeChar(',') || (beautiful && (!writeChar('\n') || !writeSpace(2 * stack.size())))) return false;
       }
       else if(stack.top() == Сondition::ObjectKey) stack.top() = Сondition::ObjectNextPair;
       else if(stack.top() ==  Сondition::Array)
       {
          if(beautiful && !writeSpace(2 * stack.size())) return false;
          stack.top() = Сondition::ArrayNextValue;
       }
    }

    return true;
}

bool JsonSAXWriter::checkIsNotObject()
{
    if(!stack.empty() && (stack.top() == Сondition::Object || stack.top() == Сondition::ObjectNextPair))
    {
       _error = InvalidOperation;
       return false;
    }

    return true;
}

bool JsonSAXWriter::checkIsObject(bool key, bool end)
{
    if(stack.empty() || (stack.top() != Сondition::Object && stack.top() != Сondition::ObjectNextPair))
    {
       _error = InvalidOperation;
       return false;
    }

    if(key && stack.top() == Сondition::ObjectNextPair)
    {
       if(!writeChar(',') || (beautiful && (!writeChar('\n') || !writeSpace(2 * stack.size())))) return false;
    }
    else if(beautiful && !writeSpace((end) ? (2 * stack.size()) - 2 : 2 * stack.size())) return false;

    return true;
}

bool JsonSAXWriter::writeString(const std::string & string)
{
    if(!writeChar('"')) return false;

    for(unsigned char c : string)
    {
        switch (c)
        {
           case '"': if(!writeChar('\\') || !writeChar('"')) return false;
           break;
           case '\\': if(!writeChar('\\') || !writeChar('\\')) return false;
           break;
           case '/': if(!writeChar('\\') || !writeChar('/')) return false;
           break;
           case '\b': if(!writeChar('\\') || !writeChar('b')) return false;
           break;
           case '\f': if(!writeChar('\\') || !writeChar('f')) return false;
           break;
           case '\n': if(!writeChar('\\') || !writeChar('n')) return false;
           break;
           case '\r': if(!writeChar('\\') || !writeChar('r')) return false;
           break;
           case '\t': if(!writeChar('\\') || !writeChar('t')) return false;
           break;
           default: if(!writeChar(c)) return false;
        }
    }

    if(!writeChar('"')) return false;

    return true;
}

void JsonSAXWriter::setError(const std::string & error){ _error = error; }

JsonSAXWriter::JsonSAXWriter(){}

std::string JsonSAXWriter::error() const { return std::move(_error); }

void JsonSAXWriter::setBuffer(JsonBufferWriter * buffer, bool beautiful)
{
    while(!stack.empty()) stack.pop();
    this->buffer = buffer;
    this->beautiful = beautiful;
}

bool JsonSAXWriter::ObjectBegin()
{
    if(!checkBuffer() || !checkIsNotObject() || !containerEnd() || !writeChar('{')) return false;
    if(beautiful && !writeChar('\n')) return false;
    stack.push(Сondition::Object);
    return true;
}

bool JsonSAXWriter::ObjectKey(const std::string & key)
{
    if(!checkBuffer() || !checkIsObject(true, false) || !writeString(key) || !writeChar(':')) return false;
    stack.top() = Сondition::ObjectKey;
    return true;
}

bool JsonSAXWriter::ObjectEnd()
{
    if(!checkBuffer() || (beautiful && !writeChar('\n')) || !checkIsObject(false, true) || !writeChar('}')) return false;
    stack.pop();
    return true;
}

bool JsonSAXWriter::ArrayBegin()
{
    if(!checkBuffer() || !checkIsNotObject() || !containerEnd() || !writeChar('[')) return false;
    if(beautiful && !writeChar('\n')) return false;
    stack.push(Сondition::Array);
    return true;
}

bool JsonSAXWriter::ArrayEnd()
{
    if(!checkBuffer()) return false;
    if(stack.empty() && (stack.top() != Сondition::Array))
    {
       _error = InvalidOperation;
       return false;
    }

    stack.pop();
    if((beautiful && (!writeChar('\n') || !writeSpace(2 * stack.size()))) || !writeChar(']')) return false;
    return true;
}

bool JsonSAXWriter::Value(const std::string & value)
{
    if(!checkBuffer() || !checkCorrectValue()) return false;
    if(!writeString(value)) return false;
    return true;
}

bool JsonSAXWriter::Value(double value)
{
    if(!checkBuffer() || !checkCorrectValue()) return false;
    std::array<char, 18> data;
    auto [ptr, ec] = std::to_chars(data.data(), data.data() + data.size(), value);

    if(ec != std::errc())
    {
       _error = ErrorConvDouble;
       return false;
    }

    std::string_view str(data.data(), ptr);
    for(unsigned char ch : str) if(!writeChar(ch)) return false;

    return true;
}

bool JsonSAXWriter::Value(long long value)
{
    if(!checkBuffer() || !checkCorrectValue()) return false;
    std::array<char, 20> data;
    auto [ptr, ec] = std::to_chars(data.data(), data.data() + data.size(), value);

    if(ec != std::errc())
    {
       _error = ErrorConvLongLong;
       return false;
    }

    std::string_view str(data.data(), ptr);
    for(unsigned char ch : str) if(!writeChar(ch)) return false;
    return true;
}

static const std::string_view S_True("true"), S_False("false"), S_Null("null");

bool JsonSAXWriter::Value(bool value)
{
    if(!checkBuffer() || !checkCorrectValue()) return false;
    if(value)for(unsigned char ch : S_True){ if(!writeChar(ch)) return false; }
    else for(unsigned char ch : S_False) if(!writeChar(ch)) return false;
    return true;
}

bool JsonSAXWriter::Null()
{
    if(!checkBuffer() || !checkCorrectValue()) return false;
    for(unsigned char ch : S_Null) if(!writeChar(ch)) return false;
    return true;
}

//-----------------------------------------------------------------------------

bool JsonWriter::writeValue(JsonValue & value)
{
    switch (value.type())
    {
       case JsonType::String: if(!Value(value.getString())) return false;
       break;
       case JsonType::Double: if(!Value(value.getDouble())) return false;
       break;
       case JsonType::LongLong: if(!Value(value.getLongLong())) return false;
       break;
       case JsonType::Bool: if(!Value(value.getBool())) return false;
       break;
       case JsonType::Null: if(!Null()) return false;
       break;
       default:
       {
          setError("Invalid json value is empty type");
          return false;
       }
       break;
    }

    return true;
}

JsonWriter::JsonWriter(){}

bool JsonWriter::write(JsonBufferWriter & buffer, const JsonValue & json, bool beautiful)
{
    if(json.type() != JsonType::Object && json.type() != JsonType::Array) return false;
    using Variant = std::variant<std::monostate,JsonValue::Object::Map::iterator,JsonValue::Array::Vector::iterator>;

    std::list<std::pair<JsonValue *, Variant>> stack;
    stack.push_back({&const_cast<JsonValue&>(json), Variant()});

    setBuffer(&buffer, beautiful);

    while(!stack.empty())
    {
       if(stack.back().first->type() == JsonType::Object)
       {
          JsonValue::Object object = *stack.back().first;
          JsonValue::Object::Map::iterator pos = (stack.back().second.index() > 0) ? std::get<1>(stack.back().second) : object.map->begin();
          if(pos == object.map->begin()){ if(!ObjectBegin()) return false; }

          bool next_container = false;
          while(pos != object.map->end())
          {
             if(!ObjectKey(pos->first)) return false;

             JsonValue & value = pos->second;
             if(value.type() == JsonType::Object || value.type() == JsonType::Array)
             {
                int count = 0;
                for(const auto & pair : stack)
                {
                    if(pair.first->value.get() == value.value.get())
                    {
                       count++;
                       break;
                    }
                }

                if(count == 0)
                {
                   pos++;
                   stack.back().second = pos;
                   stack.push_back({&value, Variant()});
                   next_container = true;
                   break;
                }
                else if(!Null()) return false;
             }
             else if(!writeValue(value)) return false;
             pos++;
          }

          if(next_container) continue;
          if(!ObjectEnd()) return false;
       }
       else
       {
          JsonValue::Array array = *stack.back().first;
          JsonValue::Array::Vector::iterator pos = (stack.back().second.index() > 0) ? std::get<2>(stack.back().second) : array.array->begin();
          if(pos == array.array->begin()){ if(!ArrayBegin()) return false; }

          bool next_container = false;
          while(pos != array.array->end())
          {
             JsonValue & value = *pos;
             if(value.type() == JsonType::Object || value.type() == JsonType::Array)
             {
                int count = 0;
                for(const auto & pair : stack)
                {
                    if(pair.first->value.get() == value.value.get())
                    {
                       count++;
                       break;
                    }
                }

                if(count == 0)
                {
                   pos++;
                   stack.back().second = pos;
                   stack.push_back({&value, Variant()});
                   next_container = true;
                   break;
                }
                else if(!Null()) return false;
             }
             else if(!writeValue(value)) return false;
             pos++;
          }

          if(next_container) continue;
          if(!ArrayEnd()) return false;
       }

       stack.pop_back();
    }

    return true;
}

bool JsonWriter::write(std::string & string, const JsonValue & json, bool beautiful)
{
    JsonStringBufferWriter buffer;
    if(!write(buffer, json, beautiful)) return false;
    string = std::move(const_cast<std::string &>(buffer.result()));
    return true;
}

std::string JsonWriter::write(const JsonValue & json, bool beautiful)
{
    std::string ret;
    write(ret, json, beautiful);
    return ret;
}

bool JsonWriter::writeToFile(const std::string & fileName, const JsonValue & json, bool beautiful)
{
    JsonFileBufferWriter buffer;
    if(!buffer.open(fileName) || !write(buffer, json, beautiful)) return false;
    return true;
}
