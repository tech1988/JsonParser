#ifndef JSON_H
#define JSON_H

#include <string>
#include <map>
#include <vector>
#include <variant>
#include <memory>
#include <stack>
#include <functional>
#include <fstream>

//Need JSON5
//Need comment
//Need parent
//Need (Up <- tree search -> Down, All)
//set recursive depth tree
//json query value

class JsonBufferReader
{
public:
    explicit JsonBufferReader(){}
    virtual ~JsonBufferReader(){}

    virtual bool next() = 0;
    virtual unsigned char value() = 0;
    virtual std::size_t offset() = 0;
};

class JsonStringViewBufferReader : public JsonBufferReader
{
    long long pos = -1;
    std::string_view _json;

public:
    explicit JsonStringViewBufferReader(std::string_view json);
    bool next() override;
    unsigned char value() override;
    std::size_t offset() override;
};

class JsonFileBufferReader : public JsonBufferReader
{
    char c = 0;
    std::size_t pos = 0;
    std::ifstream stream;
    bool is_open = false;

public:
    explicit JsonFileBufferReader();
    bool open(const std::string & fileName);
    bool isOpen();
    bool next() override;
    unsigned char value() override;
    std::size_t offset() override;
};

class JsonSAXReader
{
    std::string _error;
    bool stop;

protected:
    void stopParse();

public:

    enum Operation : unsigned char
    {
        Single,
        Multiple
    };

    explicit JsonSAXReader();
    virtual ~JsonSAXReader();

    std::string error() const;
    bool parse(JsonBufferReader & buffer, Operation operation);

    virtual void JsonBegin() = 0;
    virtual void JsonEnd() = 0;

    virtual void ObjectBegin() = 0;
    virtual void ObjectKey(const std::string & key) = 0;
    virtual void ObjectEnd() = 0;

    virtual void ArrayBegin() = 0;
    virtual void ArrayEnd() = 0;

    virtual void Value(const std::string & value) = 0;
    virtual void Value(double value) = 0;
    virtual void Value(long long value) = 0;
    virtual void Value(bool value) = 0;
    virtual void Null() = 0;
};

enum class JsonType : unsigned char
{
   Empty = 0,
   Object,
   Array,
   String,
   Double,
   LongLong,
   Bool,
   Null
};

class JsonValue final
{
    friend class JsonReader;
    friend class JsonWriter;

public:

    class Object final
    {
       friend class JsonReader;
       friend class JsonWriter;

     public:
       using Map = std::map<std::string, JsonValue>;
       explicit Object();
       Object(const Map & map);
       Object copy() const;

       std::size_t count() const;
       bool contains(const std::string & key) const;
       JsonValue value(const std::string & key) const;
       void insert(const std::string & key, const JsonValue & value) const;
       void remove(const std::string & key);
       void clear();
       JsonValue & operator [](const std::string & key) const;

       const Map & getMap() const;
       Map & getMap();
       operator const Map &() const;
       operator Map &();

       void setMap(const Map & map);
       Object & operator = (const Map & map);

     private:
       std::shared_ptr<Map> map = std::make_shared<Map>();
    };

    class Array final
    {
       friend class JsonReader;
       friend class JsonWriter;

     public:
       using Vector = std::vector<JsonValue>;
       explicit Array();
       Array(const Vector & array);
       Array copy() const;

       std::size_t count() const;
       JsonValue & at(std::size_t index) const;
       void append(const JsonValue & value);
       void clear();
       JsonValue & operator[](std::size_t index) const;

       const Vector & getVector() const;
       Vector & getVector();
       operator const Vector &() const;
       operator Vector &();

       void setVector(const Vector & vector);
       Array & operator = (const Vector & vector);

     private:
       std::shared_ptr<Vector> array = std::make_shared<Vector>();
    };

    using Value = std::variant<std::monostate, Object, Array, std::string, double, long long, bool, std::nullptr_t>;

private:
    std::shared_ptr<Value> value = std::make_shared<Value>();

public:
    explicit JsonValue();
    JsonValue(const Object & object);
    JsonValue(const Array & array);

    JsonValue(char c);
    JsonValue(const char * string);
    JsonValue(std::string_view string);
    JsonValue(const std::string & string);

    JsonValue(float val);
    JsonValue(double val);

    JsonValue(unsigned char val);
    JsonValue(short val);
    JsonValue(unsigned short val);
    JsonValue(int val);
    JsonValue(unsigned int val);
    JsonValue(long long val);

    JsonValue(bool val);
    JsonValue(std::nullptr_t);

    JsonValue copy() const;

    JsonType type() const;
    bool isEmpty() const;

    const Value & getValue() const;
    Value & getValue();
    operator const Value &() const;
    operator Value &();
    void setValue(const Value & value);
    JsonValue & operator = (const Value & value);

    Object getObject() const;
    void setObject(const Object & object);
    operator Object() const;
    JsonValue & operator = (const Object & object);

    Array getArray() const;
    void setArray(const Array & array);
    operator Array() const;
    JsonValue & operator = (const Array & array);

    std::string getString() const;
    void setString(char c);
    void setString(const char * string);
    void setString(std::string_view string);
    void setString(const std::string & string);
    operator std::string() const;
    JsonValue & operator = (char c);
    JsonValue & operator = (const char * string);
    JsonValue & operator = (std::string_view string);
    JsonValue & operator = (const std::string & string);

    double getDouble() const;
    void setDouble(float val);
    void setDouble(double val);
    operator double() const;
    JsonValue & operator = (float val);
    JsonValue & operator = (double val);

    long long getLongLong() const;
    void setLongLong(unsigned char val);
    void setLongLong(short val);
    void setLongLong(unsigned short val);
    void setLongLong(int val);
    void setLongLong(unsigned int val);
    void setLongLong(long long val);
    operator long long() const;
    JsonValue & operator = (unsigned char val);
    JsonValue & operator = (short val);
    JsonValue & operator = (unsigned short val);
    JsonValue & operator = (int val);
    JsonValue & operator = (unsigned int val);
    JsonValue & operator = (long long val);

    bool getBool() const;
    void setBool(bool val);
    operator bool() const;
    JsonValue & operator = (bool val);

    bool getNull() const;
    void setNull();
    JsonValue & operator = (std::nullptr_t);
};

class JsonReader final : public JsonSAXReader
{
    JsonValue root;
    std::stack<std::shared_ptr<JsonValue::Value>> stack;
    std::string key;
    std::function<bool(JsonValue &)> callback;

    void insertValue(JsonValue & value);

public:
    explicit JsonReader();
    bool parse(JsonBufferReader & buffer, const std::function<bool (JsonValue &)> &resultCallback, Operation operation = Single);
    bool parse(std::string_view json, const std::function<bool(JsonValue &)> & resultCallback, Operation operation = Single);
    JsonValue parse(JsonBufferReader & buffer);
    JsonValue parse(std::string_view json);
    bool parseFromFile(const std::string & fileName, const std::function<bool(JsonValue &)> & resultCallback, Operation operation = Single);
    JsonValue parseFromFile(const std::string & fileName);

private:
    void JsonBegin() override;
    void JsonEnd() override;

    void ObjectBegin() override;
    void ObjectKey(const std::string & key) override;
    void ObjectEnd() override;

    void ArrayBegin() override;
    void ArrayEnd() override;

    void Value(const std::string & value) override;
    void Value(double value) override;
    void Value(long long value) override;
    void Value(bool value) override;
    void Null() override;
};

class JsonBufferWriter
{
public:
    explicit JsonBufferWriter(){}
    virtual ~JsonBufferWriter(){}

    virtual bool write(unsigned char ch) = 0;
    virtual std::size_t writeCount() = 0;
};

class JsonStringBufferWriter : public JsonBufferWriter
{
    std::size_t count = 0;
    std::string json;

public:
    explicit JsonStringBufferWriter();
    bool write(unsigned char ch) override;
    std::size_t writeCount() override;
    const std::string & result() const;
};

class JsonFileBufferWriter : public JsonBufferWriter
{
    std::size_t count = 0;
    std::ofstream stream;
    bool is_open = false;

public:
    explicit JsonFileBufferWriter();
    bool open(const std::string & fileName);
    bool isOpen();
    bool write(unsigned char ch) override;
    std::size_t writeCount() override;
};

class JsonSAXWriter
{
    bool beautiful = false;
    std::string _error;
    JsonBufferWriter * buffer = nullptr;

    enum class Сondition : unsigned char
    {
        Object,
        ObjectKey,
        ObjectNextPair,
        Array,
        ArrayNextValue
    };

    std::stack<Сondition> stack;

    bool checkBuffer();
    bool writeChar(unsigned char ch);
    bool writeSpace(int count);
    bool checkCorrectValue();
    bool containerEnd();
    bool checkIsNotObject();
    bool checkIsObject(bool key, bool end);
    bool writeString(const std::string & string);

protected:
    void setError(const std::string & error);

public:
    explicit JsonSAXWriter();
    std::string error() const;
    void setBuffer(JsonBufferWriter * buffer, bool beautiful = false);

    bool ObjectBegin();
    bool ObjectKey(const std::string & key);
    bool ObjectEnd();

    bool ArrayBegin();
    bool ArrayEnd();

    bool Value(const std::string & value);
    bool Value(double value);
    bool Value(long long value);
    bool Value(bool value);
    bool Null();
};

class JsonWriter final : public JsonSAXWriter
{
    bool writeValue(JsonValue & value);
public:
    explicit JsonWriter();
    bool write(JsonBufferWriter & buffer, const JsonValue & json, bool beautiful = false);
    bool write(std::string & string, const JsonValue & json, bool beautiful = false);
    std::string write(const JsonValue & json, bool beautiful = false);
    bool writeToFile(const std::string & fileName, const JsonValue & json, bool beautiful = false);
};

#endif // JSON_H
