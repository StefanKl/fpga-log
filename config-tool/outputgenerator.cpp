#include "outputgenerator.h"
#include <list>
#include <string>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <QCoreApplication>
#include <QDirIterator>
#include "cobject.h"

using namespace std;
#include <iostream>
OutputGenerator::OutputGenerator(DataLogger *dataLogger, string directory) :
    dataLogger(dataLogger),
    directory(directory),
    usedIdCounter(0),
    usedTimestampSources(0),
    process(this),
    busy(false),
    error(false)
{
    cout << directory << endl;
    process.setWorkingDirectory(directory.c_str());
    connect(&process, SIGNAL(readyReadStandardOutput()), this, SLOT(newChildStdOut()));
    connect(&process, SIGNAL(readyReadStandardError()), this, SLOT(newChildErrOut()));
    connect(&process, SIGNAL(finished(int)), this, SLOT(processFinished()));

    copyProjectTemplate();
}

void OutputGenerator::copyProjectTemplate() {
    string path = "../config-tool-files/template/";
    QDirIterator dirIter(QString(path.c_str()), QDirIterator::Subdirectories);
    while (dirIter.hasNext()) {
        dirIter.next();
        if (QFileInfo(dirIter.filePath()).isFile()) {
              //  files.push_back(dirIter.filePath().toStdString());
            QString newPath = dirIter.filePath();
            newPath.remove(0, path.length());
            newPath = (directory + "/").c_str() + newPath;

            QDir().mkpath(QFileInfo(newPath).dir().absolutePath());
            QFile::copy(dirIter.filePath(), newPath);
        }
    }
}

void OutputGenerator::exec(string cmd) {
    if (!busy) {
        busy = true;
        process.start(QString(cmd.c_str()));
    } else {
        pending.push_back(cmd);
    }
}

void OutputGenerator::processFinished() {
    if (pending.empty() || error) {
        busy = false;
        emit finished(error);
    } else {
        process.start(QString(pending.front().c_str()));
        pending.pop_front();
    }
}

void OutputGenerator::newChildStdOut() {
    cout << QString(process.readAllStandardOutput()).toStdString();
}

void OutputGenerator::newChildErrOut() {
    cerr << QString(process.readAllStandardError()).toStdString();
}

void OutputGenerator::generateConfigFiles() {
    generateSystemXML();
    generateCSource();;

    exec("make jconfig +args=\"--generate system.xml\"");
}

void OutputGenerator::synthesizeSystem() {
    generateConfigFiles();

    exec("make all");
}

void OutputGenerator::generateCSource() {
    ofstream file;
    file.open(directory + "/firmware/src/logger_config.c");

    ostream& stream = file;

    writePreamble(stream);
    stream << endl;

    stringstream tmpFile;

    writeVariableDefinitions(tmpFile);
    tmpFile << endl;
    writeInitFunction(tmpFile);
    tmpFile << endl;
    writeConnectPorts(tmpFile);
    tmpFile << endl;
    writeAdvancedConfig(tmpFile);

    writeHeaderIncludes(stream);
    stream << endl << tmpFile.str();

    file.close();
    cout << "C-Konfigurationsdatei erfolgreich geschrieben." << endl;
}

void OutputGenerator::writeVariableDefinitions(std::ostream& stream) {
    map<string, CObject*> objects = dataLogger->getObjectsMap();

    for (map<string, CObject *>::iterator i = objects.begin(); i != objects.end(); i++) {
        usedHeaders.insert(i->second->getType()->getHeaderName());
        stream << i->second->getType()->getName() << "\t" << i->first << ";" << endl;
    }
}

void OutputGenerator::writeInitFunction(ostream& stream) {
    map<string, CObject*> objects = dataLogger->getObjectsMap();

    stream << "void init_objects(void) {" << endl;

    map<string, bool> initDone;
    for (map<string, CObject *>::iterator i = objects.begin(); i != objects.end(); i++) {
        writeObjectInit(stream, i->second, objects, initDone);
    }

    stream << "}" << endl;
}

void OutputGenerator::writeObjectInit(std::ostream& stream, CObject* object, std::map<string, CObject *>& objects, map<string, bool>& initDone) {
    if (!initDone[object->getName()]) {
        stringstream tmpStream;

        CMethod* init = object->getInitMethod();
        usedHeaders.insert(init->getHeaderName());
        list<CParameter>* params = init->getParameters();
        tmpStream << "  " << object->getType()->getCleanedName() << "_"
                  << init->getName() << "(&" << object->getName();
        for (list<CParameter>::iterator i = ++params->begin(); i != params->end(); i++) {
            tmpStream << ", ";

            string value  = (*i).getValue();
            try {
                CObject* paramObject = objects.at(value);
                tmpStream << "&";
                if (!initDone[value]) {
                    writeObjectInit(stream, paramObject, objects, initDone);
                }
            } catch (exception) {
                if ((*i).getDataType()->hasSuffix("_regs_t")) {
                    value = object->getName() + "_" + (*i).getName();
                    transform(value.begin(), value.end(), value.begin(), ::toupper);
                }
            }

            if (value.empty()) {
                error = true;
                cerr << "FEHLER: Parameter " << (*i).getName() << " von Objekt " << object->getName() << " nicht gesetzt!" << endl;
            }

            tmpStream << value;
        }
        stream << tmpStream.str() << ");" << endl;
        initDone[object->getName()] = true;
    }
}

void OutputGenerator::writeConnectPorts(ostream& stream) {
    stream << "void connect_ports(void) {" << endl;

    list<DatastreamObject*> modules = dataLogger->getDatastreamModules();
    for (list<DatastreamObject*>::iterator i = modules.begin(); i != modules.end(); i++) {
        DatastreamObject* module = *i;

        list<PortOut*> ports = module->getOutPorts(PORT_TYPE_DATA_OUT);
        list<PortOut*> portsC = module->getOutPorts(PORT_TYPE_CONTROL_OUT);
        ports.insert(ports.end(), portsC.begin(), portsC.end());

        for (list<PortOut*>::iterator portIt = ports.begin(); portIt != ports.end(); portIt++) {
            PortOut* port = *portIt;
            if (port->isConnected()) {
                Port* destinationPort = port->getDestination();
                DatastreamObject* destinationModule = destinationPort->getParent();

                stream << "  " << module->getType()->getCleanedName();
                if (!port->isMultiPort())
                    stream << "_set_";
                else
                    stream << "_add_";
                stream << port->getName() << "(";
                stream << "&" << module->getName() << ", ";
                stream << destinationModule->getType()->getCleanedName() << "_get_" << destinationPort->getName();
                stream << "(&" << destinationModule->getName() << ")";
                stream << ");" << endl;
            }
        }
    }

    stream << "}" << endl;
}

void OutputGenerator::writeHeaderIncludes(std::ostream& stream) {
    stream << "#include \"logger_config.h\"" << endl;
    stream << "#include <system/peripherals.h>" << endl;
    stream << "#include <fpga-log/pc_compatibility.h>" << endl << endl;
    for (set<string>::iterator i = usedHeaders.begin(); i != usedHeaders.end(); i++) {
        string s = *i;
        s.erase(0, s.find("include") + 8);
        stream << "#include <fpga-log/" << s << ">" << endl;
    }
}

void OutputGenerator::writePreamble(std::ostream& stream) {
    stream << "/*" << endl;
    stream << " * This file was auto-generated by the config-tool of fpga-log." << endl;
    stream << " * Do not change this file!" << endl;
    stream << " */" << endl;
}

void OutputGenerator::writeAdvancedConfig(std::ostream& stream) {
    stream << "void advanced_config(void) {" << endl;
    stream << "}" << endl;
}

void OutputGenerator::generateSystemXML() {
    ifstream templateFile("../config-tool-files/template.xml");
    if (!templateFile.is_open()) {
        cerr << "System Template XML konnte nicht geöffnet werden." << endl;
        return;
    }

    ofstream file;
    file.open(directory + "/system.xml");
    ostream& stream = file;

    QString targetNode;
    QXmlStreamWriter targetWriter(&targetNode);
    targetWriter.setAutoFormatting(true);
    writeTargetNode(targetWriter);

    QString peripherals;
    QXmlStreamWriter peripheralsWriter(&peripherals);
    peripheralsWriter.setAutoFormatting(true);
    writePeripherals(peripheralsWriter);

    QString pins;
    QXmlStreamWriter pinsWriter(&pins);
    pinsWriter.setAutoFormatting(true);
    writePins(pinsWriter);

    usedTimestampSources = max(1, usedTimestampSources);
    while (!templateFile.eof()) {
        string line;
        getline(templateFile, line);
        if (line.compare("TARGET_NODE") == 0) {
            stream << targetNode.toStdString() << endl;
        } else if (line.compare("FPGA-LOG_PERIPHERALS") == 0) {
            stream << peripherals.toStdString() << endl;
        } else if (line.compare("PERI_CLOCK_ATTRIBUTE") == 0) {
            stream << "<attribute id=\"value\">" << dataLogger->getPeriClk() << "</attribute>" << endl;
        } else if (line.compare("CLOCK_PERIOD_ATTRIBUTE") == 0) {
            stream << "<attribute id=\"value\">" << (1000000000.0f / dataLogger->getClk()) << "</attribute>" << endl;
        } else if (line.compare("TIMESTAMP_GEN_SOURCES_ATTRIBUTE") == 0) {
            stream << "<attribute id=\"value\">" << usedTimestampSources << "</attribute>" << endl;
        } else if (line.compare("FPGA_PINS") == 0) {
            stream << pins.toStdString() << endl;
        } else {
            stream << line << endl;
        }
    }
    file.close();
    cout << "System Konfigurationsdatei erfolgreich geschrieben." << endl;
}

void OutputGenerator::writeAttributeElement(QXmlStreamWriter& writer, QString id, QString text) {
    writer.writeStartElement("attribute");
    writer.writeAttribute("id", id);
    writer.writeCharacters(text);
    writer.writeEndElement();
}

void OutputGenerator::writeTargetNode(QXmlStreamWriter& writer) {
    writer.writeStartElement("node");
    writer.writeAttribute("id", "TARGET");
    writeAttributeElement(writer, "preset", dataLogger->getTarget()->getValue().c_str());
    writer.writeEndElement();
}

void OutputGenerator::writeParameter(QXmlStreamWriter& writer, CParameter* parameter) {
    writer.writeStartElement("parameter");
    string name = parameter->getName();
    string pname = name;
    transform(pname.begin(), pname.end(), pname.begin(), ::toupper);
    pname = "#PARAM." + pname;
    writer.writeAttribute("id", pname.c_str());
    writeAttributeElement(writer, "name", name.c_str());
    if (name.compare("CLOCK_FREQUENCY") == 0) {
        writeAttributeElement(writer, "value", to_string(dataLogger->getPeriClk()).c_str());
    } else {
        writeAttributeElement(writer, "value", parameter->getValue().c_str());
    }
    writer.writeEndElement();
}

void OutputGenerator::writePeripheral(QXmlStreamWriter& writer, SpmcPeripheral* peripheral) {
    writer.writeStartElement("peripheral");
    string peripheralName = peripheral->getCompleteName().c_str();
    string peripheralNameUpper = peripheralName;
    transform(peripheralNameUpper.begin(), peripheralNameUpper.end(), peripheralNameUpper.begin(), ::toupper);
    writer.writeAttribute("id", peripheralNameUpper.c_str());
    string type = peripheral->getDataType()->getCleanedName();
    type.erase(type.length() - 5, type.length());
    writeAttributeElement(writer, "module_type", type.c_str());
    writeAttributeElement(writer, "name", peripheralName.c_str());

    list<CParameter*> parameters = peripheral->getParameters();
    for (list<CParameter*>::iterator i = parameters.begin(); i != parameters.end(); i++) {
        writeParameter(writer, *i);
    }

    map<string, list<PeripheralPort*> > ports = peripheral->getPorts();
    for (map<string, list<PeripheralPort*> >::iterator groupIt = ports.begin(); groupIt != ports.end(); groupIt++) {
        list<PeripheralPort*> group = groupIt->second;
        for (list<PeripheralPort*>::iterator i = group.begin(); i != group.end(); i++) {
            PeripheralPort* port = *i;
            writer.writeStartElement("port");
            string portName = port->getName();
            string portId = "#PORT." + portName;
            writer.writeAttribute("id", portId.c_str());

            writeAttributeElement(writer, "name", portName.c_str());

            list<CParameter*> lines = port->getLines();
            int lsb = 0;
            for (list<CParameter*>::iterator portIt = lines.begin(); portIt != lines.end(); portIt++) {
                QString destination = (*portIt)->getValue().c_str();
                if (!destination.isEmpty()) {
                    if (destination.contains(":")) { //pin destination
                        destination.replace(":", "_");
                        usedPins.push_back(FpgaPin(destination.toStdString(), port->getDirection()));
                        destination = "#PIN." + destination;
                    } else if (destination.startsWith("./")) { //other peripheral of this module as destination
                        destination.remove(0, 2);
                        string destName = destination.toStdString();
                        int slash = destName.find("/");
                        destName.erase(slash, destName.length());
                        destName = "SUBSYSTEM/" + peripheral->getParentName() + "_" + destName;
                        transform(destName.begin(), destName.end(), destName.begin(), ::toupper);
                        destination.remove(0, slash);
                        destination = destName.c_str() + destination;
                    } else if (destination.compare("TIMESTAMP_GEN") == 0) { //timestamp generator source
                        destination = "SUBSYSTEM/TIMESTAMP_GEN/#PORT.source";
                        string param = (*portIt)->getName();
                        peripheral->getParentObject()->getInitMethod()->getParameter(param)->setValue(to_string(usedTimestampSources++));
                    }
                    writeConnection(writer, destination.toStdString(), lsb++);
                }
            }

            writer.writeEndElement();
        }
    }

    writeSpmcConnections(writer);

    writer.writeEndElement();
}

void OutputGenerator::writePeripherals(QXmlStreamWriter& writer) {
    map<string, CObject*> objects = dataLogger->getObjectsMap();
    for (map<string, CObject*>::iterator i = objects.begin(); i != objects.end(); i++) {
        list<SpmcPeripheral*> peripherals = i->second->getPeripherals();
        for (list<SpmcPeripheral*>::iterator ip = peripherals.begin(); ip != peripherals.end(); ip++) {
            writePeripheral(writer, *ip);
        }
    }
}

void OutputGenerator::writeConnection(QXmlStreamWriter& writer, std::string destination, int lsb) {
    writer.writeStartElement("connection");
    string connectionId = "#" + to_string(usedIdCounter++);
    writer.writeAttribute("id", connectionId.c_str());
    destination = "/TOP_MODULE/" + destination;
    writeAttributeElement(writer, "connected_port", destination.c_str());
    writeAttributeElement(writer, "lsb_index", to_string(lsb).c_str());

    writer.writeEndElement();
}

void OutputGenerator::writePortConnection(QXmlStreamWriter& writer, std::string port, string destination, int lsb) {
    writer.writeStartElement("port");
    string portId = "#PORT." + port;
    writer.writeAttribute("id", portId.c_str());
    writeAttributeElement(writer, "name", port.c_str());

    writeConnection(writer, destination, lsb);

    writer.writeEndElement();
}

void OutputGenerator::writeSpmcConnections(QXmlStreamWriter& writer) {
    writePortConnection(writer, "access_peri", "SUBSYSTEM/SPARTANMC/#PORT.access_peri", 0);
    writePortConnection(writer, "addr_peri", "SUBSYSTEM/SPARTANMC/#PORT.addr_peri", 0);
    writePortConnection(writer, "clk_peri", "SUBSYSTEM/SPARTANMC/#PORT.clk_peri", 0);
    writePortConnection(writer, "do_peri", "SUBSYSTEM/SPARTANMC/#PORT.do_peri", 0);
    writePortConnection(writer, "reset", "SUBSYSTEM/SPARTANMC/#PORT.reset", 0);
    writePortConnection(writer, "wr_peri", "SUBSYSTEM/SPARTANMC/#PORT.wr_peri", 0);
    writePortConnection(writer, "di_peri", "SUBSYSTEM/SPARTANMC/#PORT.di_peri", 0);
}

void OutputGenerator::writeClkPin(QXmlStreamWriter& writer) {
    writer.writeStartElement("fpga_pin");
    CParameter* clkPinParam = dataLogger->getClockPin();
    QString pinName = clkPinParam->getValue().c_str();
    pinName.replace(":", "_");
    writer.writeAttribute("id", "#PIN.CLK");

    writeAttributeElement(writer, "name", pinName);
    writeAttributeElement(writer, "direction", "INPUT");
    writeAttributeElement(writer, "io_standard", "LVCMOS33");

    writer.writeEndElement();
}

void OutputGenerator::writePins(QXmlStreamWriter& writer) {
    writeClkPin(writer);

    for (list<FpgaPin>::iterator i = usedPins.begin(); i != usedPins.end(); i++) {
         writer.writeStartElement("fpga_pin");
         QString pinName = (*i).getName().c_str();
         pinName.replace(":", "_");
         string pinId = "#PIN." + pinName.toStdString();
         writer.writeAttribute("id", pinId.c_str());

         writeAttributeElement(writer, "name", pinName);
         writeAttributeElement(writer, "direction", (*i).getDirection().c_str());
         writeAttributeElement(writer, "io_standard", "LVCMOS33");

         writer.writeEndElement();
    }
}

void OutputGenerator::flash() {
    exec("./flash.sh");
}