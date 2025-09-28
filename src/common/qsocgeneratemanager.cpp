// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025 Huang Rui <vowstar@gmail.com>

#include "common/qsocgeneratemanager.h"
#include "common/qsocgenerateprimitiveclock.h"
#include "common/qsocgenerateprimitivecomb.h"
#include "common/qsocgenerateprimitivefsm.h"
#include "common/qsocgenerateprimitivepower.h"
#include "common/qsocgenerateprimitivereset.h"
#include "common/qsocgenerateprimitiveseq.h"
#include "common/qstaticstringweaver.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>

#include <fstream>
#include <iostream>

QSocGenerateManager::QSocGenerateManager(
    QObject            *parent,
    QSocProjectManager *projectManager,
    QSocModuleManager  *moduleManager,
    QSocBusManager     *busManager,
    QLLMService        *llmService)
    : QObject(parent)
    , projectManager(projectManager)
    , moduleManager(moduleManager)
    , busManager(busManager)
    , llmService(llmService)
    , resetPrimitive(new QSocResetPrimitive(this))
    , clockPrimitive(new QSocClockPrimitive(this))
    , powerPrimitive(new QSocPowerPrimitive(this))
    , fsmPrimitive(new QSocFSMPrimitive(this))
    , combPrimitive(new QSocCombPrimitive(this))
    , seqPrimitive(new QSocSeqPrimitive(this))
{
    /* All private members set by constructor */
}

QSocGenerateManager::~QSocGenerateManager()
{
    delete resetPrimitive;
    delete clockPrimitive;
    delete powerPrimitive;
    delete fsmPrimitive;
    delete combPrimitive;
    delete seqPrimitive;
}

void QSocGenerateManager::setProjectManager(QSocProjectManager *projectManager)
{
    this->projectManager = projectManager;
}

void QSocGenerateManager::setModuleManager(QSocModuleManager *moduleManager)
{
    this->moduleManager = moduleManager;
}

void QSocGenerateManager::setBusManager(QSocBusManager *busManager)
{
    this->busManager = busManager;
}

void QSocGenerateManager::setLLMService(QLLMService *llmService)
{
    this->llmService = llmService;
}

QSocProjectManager *QSocGenerateManager::getProjectManager()
{
    return projectManager;
}

QSocModuleManager *QSocGenerateManager::getModuleManager()
{
    return moduleManager;
}

QSocBusManager *QSocGenerateManager::getBusManager()
{
    return busManager;
}

QLLMService *QSocGenerateManager::getLLMService()
{
    return llmService;
}

void QSocGenerateManager::setForceOverwrite(bool force)
{
    forceOverwrite = force;

    // Propagate force setting to all primitive generators
    if (clockPrimitive) {
        clockPrimitive->setForceOverwrite(force);
    }
    if (resetPrimitive) {
        resetPrimitive->setForceOverwrite(force);
    }
    if (powerPrimitive) {
        powerPrimitive->setForceOverwrite(force);
    }
}

QString QSocGenerateManager::cleanTypeForWireDeclaration(const QString &typeStr)
{
    if (typeStr.isEmpty()) {
        return {};
    }

    QString cleaned = typeStr;

    /* Remove leading whitespace + keyword + keyword trailing whitespace */
    static const QRegularExpression regularExpression(R"(\s*[A-Za-z_]+\s*(?=\[|\s*$))");
    /* Explanation:
     *   \s*           optional leading whitespace
     *   [A-Za-z_]+    keyword (only letters and underscores)
     *   \s*           whitespace after keyword
     *   (?=\[|\s*$)   only match when followed by '[' or whitespace until end of line
     */
    cleaned.replace(regularExpression, "");

    /* Clean up any remaining whitespace */
    cleaned = cleaned.trimmed();

    return cleaned;
}
