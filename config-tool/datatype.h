#ifndef DATATYPE_H
#define DATATYPE_H

#include <string>
#include <list>
#include <map>
#include <QWidget>
#include "cmethod.h"

class DataLogger;

class DataType
{
public:
    DataType(std::string name);
    virtual ~DataType() {}

    std::string getName() { return name; }
    std::string getCleanedName();
    std::string getDisplayName() { return getCleanedName(); }
    bool hasPrefix(std::string prefix) { return (name.find(prefix) == 0); }
    bool hasSuffix(std::string suffix) { return (name.rfind(suffix) == (name.length() - suffix.length())); }

    virtual QWidget* getConfigWidget(DataLogger*, std::string);
    virtual std::string getConfigData(QWidget*) { return ""; }

    static DataType* getType(std::string name) { return types.at(name); }
    static std::map<std::string, DataType*> getTypes() { return types; }
private:
    std::string name;

    static std::map<std::string, DataType*> types;
};

class DataTypeStruct : public DataType {
public:
    DataTypeStruct(std::string name);
    virtual ~DataTypeStruct() {}

    DataTypeStruct* getSuperType() { return super; }
    std::list<DataTypeStruct*> getChilds() { return childs; }
    std::list<CMethod*> getMethods() { return methods; }
    CMethod* getMethod(std::string methodName);
    bool isInstantiableObject();
    bool isSuperclass(DataTypeStruct* dataType);

    void setSuperType(DataTypeStruct* super) { this->super = super; }
    void addChild(DataTypeStruct* child) { childs.push_back(child); }
    void addMethod(CMethod* method) { methods.push_back(method); }

    virtual QWidget* getConfigWidget(DataLogger* dataLogger, std::string startValue);
    virtual std::string getConfigData(QWidget* widget);

    static DataTypeStruct* getType(std::string name) { return types.at(name); }
    static std::map<std::string, DataTypeStruct*> getTypes() { return types; }
private:
    DataTypeStruct* super;

    std::list<DataTypeStruct*> childs;
    std::list<CMethod*> methods;

    static std::map<std::string, DataTypeStruct*> types;
};

class DataTypeNumber : public DataType {
public:
    DataTypeNumber(std::string name, long min, long max);
    virtual ~DataTypeNumber() {}

    virtual QWidget* getConfigWidget(DataLogger*, std::string startValue);
    virtual std::string getConfigData(QWidget* widget);
private:
    long min;
    long max;
};

class DataTypeEnumeration : public DataType {
public:
    DataTypeEnumeration(std::string name);
    virtual ~DataTypeEnumeration() {}

    void addValue(std::string value) { values.push_back(value); }
    void addValues(std::list<std::string> valueList) { values.insert(values.end(), valueList.begin(), valueList.end()); }

    virtual QWidget* getConfigWidget(DataLogger*, std::string startValue);
    virtual std::string getConfigData(QWidget* widget);
private:
    std::list<std::string> values;
};

class DataTypeString : public DataType {
public:
    DataTypeString(std::string name);
    virtual ~DataTypeString() {}

    virtual QWidget* getConfigWidget(DataLogger*, std::string startValue);
    virtual std::string getConfigData(QWidget* widget);
};

#endif // DATATYPE_H
