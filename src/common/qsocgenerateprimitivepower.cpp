// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025 Huang Rui <vowstar@gmail.com>

#include "qsocgenerateprimitivepower.h"
#include "qsocgeneratemanager.h"
#include "qsocverilogutils.h"
#include <cmath>
#include <QDebug>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QSet>

QSocPowerPrimitive::QSocPowerPrimitive(QSocGenerateManager *parent)
    : m_parent(parent)
{}

void QSocPowerPrimitive::setForceOverwrite(bool force)
{
    m_forceOverwrite = force;
}

bool QSocPowerPrimitive::generatePowerController(const YAML::Node &powerNode, QTextStream &out)
{
    if (!powerNode || !powerNode.IsMap()) {
        qWarning() << "Invalid power node provided";
        return false;
    }

    // Parse configuration
    PowerControllerConfig config = parsePowerConfig(powerNode);

    if (config.domains.isEmpty()) {
        qWarning() << "Power configuration must have at least one domain";
        return false;
    }

    // Generate or update power_cell.v file
    if (m_parent && m_parent->getProjectManager()) {
        QString outputDir = m_parent->getProjectManager()->getOutputPath();
        if (!generatePowerCellFile(outputDir)) {
            qWarning() << "Failed to generate power_cell.v file";
            return false;
        }
    }

    // Generate Verilog code
    generateModuleHeader(config, out);
    generateWireDeclarations(config, out);
    generatePowerLogic(config, out);
    generateOutputAssignments(config, out);

    // Close module
    out << "\nendmodule\n\n";

    return true;
}

QSocPowerPrimitive::PowerControllerConfig QSocPowerPrimitive::parsePowerConfig(
    const YAML::Node &powerNode)
{
    PowerControllerConfig config;

    // Parse basic properties
    if (!powerNode["name"]) {
        qCritical() << "Error: 'name' field is required in power configuration";
        qCritical() << "Example: power: { name: pwr0, ... }";
        return config;
    }
    config.name       = QString::fromStdString(powerNode["name"].as<std::string>());
    config.moduleName = config.name; // Use same name for module

    // Host clock and reset (required for FSM)
    if (!powerNode["host_clock"]) {
        qCritical() << "Error: 'host_clock' field is required in power configuration";
        return config;
    }
    config.host_clock = QString::fromStdString(powerNode["host_clock"].as<std::string>());

    if (!powerNode["host_reset"]) {
        qCritical() << "Error: 'host_reset' field is required in power configuration";
        return config;
    }
    config.host_reset = QString::fromStdString(powerNode["host_reset"].as<std::string>());

    // DFT test enable (optional)
    if (powerNode["test_enable"] && powerNode["test_enable"].IsScalar()) {
        config.test_enable = QString::fromStdString(powerNode["test_enable"].as<std::string>());
    }

    // Parse domains
    if (powerNode["domain"] && powerNode["domain"].IsSequence()) {
        for (size_t i = 0; i < powerNode["domain"].size(); ++i) {
            const YAML::Node &domainNode = powerNode["domain"][i];
            if (!domainNode.IsMap())
                continue;

            PowerDomain domain;

            // Domain name (required)
            if (!domainNode["name"]) {
                qCritical() << "Error: 'name' field is required for each domain";
                continue;
            }
            domain.name = QString::fromStdString(domainNode["name"].as<std::string>());

            // Cache YAML node for type inference
            m_domainYamlCache[domain.name] = domainNode;

            // Parse dependencies (optional, absence = AO, empty array = root)
            if (domainNode["depend"] && domainNode["depend"].IsSequence()) {
                for (size_t j = 0; j < domainNode["depend"].size(); ++j) {
                    const YAML::Node &depNode = domainNode["depend"][j];
                    if (!depNode.IsMap())
                        continue;

                    Dependency dep;
                    if (depNode["name"]) {
                        dep.name = QString::fromStdString(depNode["name"].as<std::string>());
                    }
                    if (depNode["type"]) {
                        dep.type = QString::fromStdString(depNode["type"].as<std::string>());
                    } else {
                        dep.type = "hard"; // Default to hard dependency
                    }
                    domain.depends.append(dep);
                }
            }

            // Voltage (optional)
            domain.v_mv = domainNode["v_mv"] ? domainNode["v_mv"].as<int>() : 0;

            // Power good signal
            if (domainNode["pgood"]) {
                domain.pgood = QString::fromStdString(domainNode["pgood"].as<std::string>());
            }

            // Timing parameters
            domain.wait_dep   = domainNode["wait_dep"] ? domainNode["wait_dep"].as<int>() : 0;
            domain.settle_on  = domainNode["settle_on"] ? domainNode["settle_on"].as<int>() : 0;
            domain.settle_off = domainNode["settle_off"] ? domainNode["settle_off"].as<int>() : 0;

            // Follow entries for reset synchronization
            if (domainNode["follow"] && domainNode["follow"].IsSequence()) {
                for (size_t j = 0; j < domainNode["follow"].size(); ++j) {
                    const YAML::Node &followNode = domainNode["follow"][j];
                    if (!followNode.IsMap())
                        continue;

                    FollowEntry entry;
                    if (followNode["clock"]) {
                        entry.clock = QString::fromStdString(followNode["clock"].as<std::string>());
                    }
                    if (followNode["reset"]) {
                        entry.reset = QString::fromStdString(followNode["reset"].as<std::string>());
                    }
                    entry.stage = followNode["stage"] ? followNode["stage"].as<int>()
                                                      : 4; // Default to 4 stages

                    // Only add valid entries with strict validation
                    if (!entry.clock.isEmpty() && !entry.reset.isEmpty()) {
                        // FATAL: Check for host signal misuse (creates circular dependency)
                        if (entry.clock == config.host_clock) {
                            qCritical()
                                << "FATAL: Domain" << domain.name
                                << "follow entry cannot use host_clock" << config.host_clock
                                << "as synchronization clock - this creates circular dependency!";
                            continue; // Skip this erroneous configuration
                        }
                        if (entry.reset == config.host_reset) {
                            qCritical() << "FATAL: Domain" << domain.name
                                        << "follow entry cannot use host_reset" << config.host_reset
                                        << "as reset output - this creates port conflict!";
                            continue; // Skip this erroneous configuration
                        }
                        domain.follow_entries.append(entry);
                    }
                }
            }

            config.domains.append(domain);
        }
    }

    return config;
}

void QSocPowerPrimitive::generateModuleHeader(const PowerControllerConfig &config, QTextStream &out)
{
    out << "/* " << config.moduleName << " - Power Controller\n";
    out << " * Generated by QSoC Power Primitive\n";
    out << " */\n\n";

    out << "module " << config.moduleName << " (\n";

    // Initialize port tracking for "output win" mechanism
    QSet<QString> addedSignals;
    QStringList   portDecls;
    QStringList   portComments;

    // Host clock and reset (inputs)
    portDecls << QString("    input  wire %1").arg(config.host_clock);
    portComments << "/**< Host clock (typically AO) */";
    addedSignals.insert(config.host_clock);

    portDecls << QString("    input  wire %1").arg(config.host_reset);
    portComments << "/**< Host reset (typically AO) */";
    addedSignals.insert(config.host_reset);

    // DFT test enable (optional, check for duplicates)
    if (!config.test_enable.isEmpty() && !addedSignals.contains(config.test_enable)) {
        portDecls << QString("    input  wire %1").arg(config.test_enable);
        portComments << "/**< DFT test enable */";
        addedSignals.insert(config.test_enable);
    }

    // System reset for reset synchronization (check for duplicates)
    if (!addedSignals.contains("rst_sys_n")) {
        portDecls << QString("    input  wire rst_sys_n");
        portComments << "/**< System reset for domain sync */";
        addedSignals.insert("rst_sys_n");
    }

    // Power good inputs (check for duplicates)
    for (const auto &domain : config.domains) {
        if (!domain.pgood.isEmpty() && !addedSignals.contains(domain.pgood)) {
            portDecls << QString("    input  wire %1").arg(domain.pgood);
            portComments << QString("/**< %1 voltage good */").arg(domain.name);
            addedSignals.insert(domain.pgood);
        }
    }

    // Control inputs (enable and fault clear, check for duplicates)
    for (const auto &domain : config.domains) {
        YAML::Node yamlNode = m_domainYamlCache.value(domain.name);
        if (!isAODomain(domain, yamlNode)) {
            QString enableName = QString("en_%1").arg(domain.name);
            QString clearName  = QString("clr_%1").arg(domain.name);

            if (!addedSignals.contains(enableName)) {
                portDecls << QString("    input  wire %1").arg(enableName);
                portComments << QString("/**< Enable %1 */").arg(domain.name);
                addedSignals.insert(enableName);
            }

            if (!addedSignals.contains(clearName)) {
                portDecls << QString("    input  wire %1").arg(clearName);
                portComments << QString("/**< Clear fault for %1 */").arg(domain.name);
                addedSignals.insert(clearName);
            }
        }
    }

    // ICG enable outputs (check for duplicates)
    for (const auto &domain : config.domains) {
        QString icgName = QString("icg_en_%1").arg(domain.name);
        if (!addedSignals.contains(icgName)) {
            portDecls << QString("    output wire %1").arg(icgName);
            portComments << QString("/**< ICG enable for %1 */").arg(domain.name);
            addedSignals.insert(icgName);
        }
    }

    // NOTE: rst_gate_*_n are internal signals, not module ports

    // Domain clock inputs for reset synchronizers (follow entries)
    for (const auto &domain : config.domains) {
        for (const auto &entry : domain.follow_entries) {
            if (!addedSignals.contains(entry.clock)) {
                portDecls << QString("    input  wire %1").arg(entry.clock);
                portComments << QString("/**< Domain clock for %1 reset sync */").arg(domain.name);
                addedSignals.insert(entry.clock);
            }
        }
    }

    // Power switch outputs (check for duplicates)
    for (const auto &domain : config.domains) {
        YAML::Node yamlNode = m_domainYamlCache.value(domain.name);
        if (!isAODomain(domain, yamlNode)) {
            QString switchName = QString("sw_%1").arg(domain.name);
            if (!addedSignals.contains(switchName)) {
                portDecls << QString("    output wire %1").arg(switchName);
                portComments << QString("/**< Switch for %1 */").arg(domain.name);
                addedSignals.insert(switchName);
            }
        }
    }

    // Reset synchronizer outputs (follow entries, OUTPUT WIN over inputs)
    for (const auto &domain : config.domains) {
        for (const auto &entry : domain.follow_entries) {
            if (!addedSignals.contains(entry.reset)) {
                portDecls << QString("    output wire %1").arg(entry.reset);
                portComments << QString("/**< Synchronized reset for %1 */").arg(domain.name);
                addedSignals.insert(entry.reset);
            }
        }
    }

    // Status outputs (check for duplicates)
    for (const auto &domain : config.domains) {
        QString readyName = QString("rdy_%1").arg(domain.name);
        QString faultName = QString("flt_%1").arg(domain.name);

        if (!addedSignals.contains(readyName)) {
            portDecls << QString("    output wire %1").arg(readyName);
            portComments << QString("/**< %1 ready */").arg(domain.name);
            addedSignals.insert(readyName);
        }

        if (!addedSignals.contains(faultName)) {
            portDecls << QString("    output wire %1").arg(faultName);
            portComments << QString("/**< %1 fault */").arg(domain.name);
            addedSignals.insert(faultName);
        }
    }

    // Output all ports with unified boundary judgment
    for (int i = 0; i < portDecls.size(); ++i) {
        bool    isLast = (i == portDecls.size() - 1);
        QString comma  = isLast ? "" : ",";
        out << portDecls[i] << comma << " " << portComments[i] << "\n";
    }

    out << ");\n\n";
}

void QSocPowerPrimitive::generateWireDeclarations(
    const PowerControllerConfig &config, QTextStream &out)
{
    out << "    /* Internal wires for dependency aggregation */\n";
    out << "    /* Note: Configure soft dependencies in YAML with type:'soft' */\n";
    out << "    /* Example YAML soft dependency: */\n";
    out << "    /*   - name: c906 */\n";
    out << "    /*     depend: */\n";
    out << "    /*       - name: sram */\n";
    out << "    /*         type: soft   # This makes rdy_sram a soft dependency */\n";
    out << "    /* Generated wire: dep_soft_all_c906 = rdy_sram; */\n";
    out << "    /* If all dependencies are 'hard' (default), dep_soft_all remains 1'b1 */\n";

    for (const auto &domain : config.domains) {
        QString hardSig = getHardDependencySignal(domain);
        QString softSig = getSoftDependencySignal(domain);

        if (hardSig != "1'b1") {
            out << "    wire dep_hard_all_" << domain.name << " = " << hardSig << ";\n";
        }
        if (softSig != "1'b1") {
            out << "    wire dep_soft_all_" << domain.name << " = " << softSig
                << "; /* Soft dependencies */\n";
        } else if (!domain.depends.isEmpty()) {
            // Has dependencies but none are soft - add comment for clarity
            out << "    /* " << domain.name
                << ": no soft dependencies (all hard), dep_soft_all tied to 1'b1 */\n";
        }
    }

    out << "\n";
}

void QSocPowerPrimitive::generatePowerLogic(const PowerControllerConfig &config, QTextStream &out)
{
    // Generate internal wire declarations for rst_gate_n signals
    out << "    /* Internal wires for FSM reset gates */\n";
    for (const auto &domain : config.domains) {
        out << "    wire rst_gate_" << domain.name << "_n;\n";
    }
    out << "\n";

    out << "    /* Power FSM instances */\n";

    for (const auto &domain : config.domains) {
        YAML::Node yamlNode = m_domainYamlCache.value(domain.name);
        bool       isAO     = isAODomain(domain, yamlNode);
        bool       isRoot   = isRootDomain(domain, yamlNode);

        out << "    /* " << domain.name << ": ";
        if (isAO) {
            out << "AO domain (no depend key) */\n";
        } else if (isRoot) {
            out << "Root domain (depend: []) */\n";
        } else {
            out << "Normal domain */\n";
        }

        QString hardSig = getHardDependencySignal(domain);
        QString softSig = getSoftDependencySignal(domain);

        out << "    qsoc_power_fsm #(\n";
        out << "        .HAS_SWITCH        (" << (isAO ? "0" : "1") << "),\n";
        out << "        .WAIT_DEP_CYCLES   (" << domain.wait_dep << "),\n";
        out << "        .SETTLE_ON_CYCLES  (" << domain.settle_on << "),\n";
        out << "        .SETTLE_OFF_CYCLES (" << domain.settle_off << ")\n";
        out << "    ) u_pwr_" << domain.name << " (\n";
        out << "        .clk          (" << config.host_clock << "),\n";
        out << "        .rst_n        (" << config.host_reset << "),\n";

        if (!config.test_enable.isEmpty()) {
            out << "        .test_en      (" << config.test_enable << "),\n";
        } else {
            out << "        .test_en      (1'b0),\n";
        }

        if (isAO) {
            out << "        .ctrl_enable  (1'b1), /**< AO always on */\n";
            out << "        .fault_clear  (1'b0),\n";
        } else {
            out << "        .ctrl_enable  (en_" << domain.name << "),\n";
            out << "        .fault_clear  (clr_" << domain.name << "),\n";
        }

        if (hardSig == "1'b1") {
            out << "        .dep_hard_all (1'b1),\n";
        } else {
            out << "        .dep_hard_all (dep_hard_all_" << domain.name << "),\n";
        }

        if (softSig == "1'b1") {
            out << "        .dep_soft_all (1'b1),\n";
        } else {
            out << "        .dep_soft_all (dep_soft_all_" << domain.name << "),\n";
        }

        if (!domain.pgood.isEmpty()) {
            out << "        .pgood        (" << domain.pgood << "),\n";
        } else {
            out << "        .pgood        (1'b1),\n";
        }

        out << "        .clk_enable   (icg_en_" << domain.name << "),\n";
        out << "        .rst_gate_n   (rst_gate_" << domain.name << "_n),\n";

        if (isAO) {
            out << "        .pwr_switch   (), /**< Unused for AO */\n";
        } else {
            out << "        .pwr_switch   (sw_" << domain.name << "),\n";
        }

        out << "        .ready        (rdy_" << domain.name << "),\n";
        out << "        .valid        (), /**< Optional, not exported */\n";
        out << "        .fault        (flt_" << domain.name << ")\n";
        out << "    );\n\n";

        // Generate reset synchronizers for follow entries
        if (!domain.follow_entries.isEmpty()) {
            out << "    /* Reset synchronizers for " << domain.name << " domain */\n";
            for (int i = 0; i < domain.follow_entries.size(); ++i) {
                const auto &entry = domain.follow_entries[i];
                out << "    qsoc_power_rst_sync #(\n";
                out << "        .STAGE (" << entry.stage << ")\n";
                out << "    ) u_rst_sync_" << domain.name << "_" << i << " (\n";
                out << "        .clk_dom     (" << entry.clock << "),\n";
                out << "        .rst_gate_n  (rst_sys_n & rst_gate_" << domain.name << "_n),\n";
                if (!config.test_enable.isEmpty()) {
                    out << "        .test_en     (" << config.test_enable << "),\n";
                } else {
                    out << "        .test_en     (1'b0),\n";
                }
                out << "        .rst_dom_n   (" << entry.reset << ")\n";
                out << "    );\n\n";
            }
        }
    }
}

void QSocPowerPrimitive::generateOutputAssignments(
    const PowerControllerConfig &config, QTextStream &out)
{
    Q_UNUSED(config);
    // No additional assignments needed - all outputs come directly from FSM instances
    out << "    /* All outputs are directly connected from FSM instances */\n";
}

bool QSocPowerPrimitive::generatePowerCellFile(const QString &outputDir)
{
    QString filePath = QDir(outputDir).filePath("power_cell.v");

    // Check if file exists and is complete
    if (!m_forceOverwrite && isPowerCellFileComplete(filePath)) {
        qInfo() << "power_cell.v already exists and is complete, skipping generation";
        return true;
    }

    // Generate power_cell.v
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to open file for writing:" << filePath;
        return false;
    }

    QTextStream out(&file);
    out << generatePowerFSMModule();
    out << "\n" << generateResetPipeModule();
    file.close();

    qInfo() << "Generated power_cell.v at:" << filePath;
    return true;
}

bool QSocPowerPrimitive::isPowerCellFileComplete(const QString &filePath)
{
    QFile file(filePath);
    if (!file.exists()) {
        return false;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QString content = QTextStream(&file).readAll();
    file.close();

    // Check if both qsoc_power_fsm and qsoc_power_rst_sync modules exist
    return content.contains("module qsoc_power_fsm")
           && content.contains("module qsoc_power_rst_sync");
}

QString QSocPowerPrimitive::generatePowerFSMModule()
{
    QString     code;
    QTextStream out(&code);

    out << "/* power_cell.v\n";
    out << " * Module: qsoc_power_fsm\n";
    out << " * Minimal per-domain power controller with strict power sequencing\n";
    out << " * Power-up   : enable switch -> wait pgood+settle -> clock on -> release reset\n";
    out << " * Power-down : assert reset -> clock off and disable switch -> wait drop+settle\n";
    out << " * Hard depend blocks on timeout and enters FAULT with auto-heal\n";
    out << " * Soft depend warns on timeout and proceeds\n";
    out << " * test_en=1 forces power on, clock on, reset released, while FSM state is preserved\n";
    out << " */\n\n";

    out << "module qsoc_power_fsm\n";
    out << "#(\n";
    out << "    /* Parameters appear before ports as required by Verilog-2005. */\n";
    out << "    parameter integer HAS_SWITCH        = 1,   /**< 1=drive switch              */\n";
    out << "    parameter integer WAIT_DEP_CYCLES   = 100, /**< depend wait window (cycles) */\n";
    out << "    parameter integer SETTLE_ON_CYCLES  = 100, /**< power-on settle (cycles)    */\n";
    out << "    parameter integer SETTLE_OFF_CYCLES = 50   /**< power-off settle cycles     */\n";
    out << ")\n";
    out << "(\n";
    out << "    /* AO host clock/reset only */\n";
    out << "    input  wire clk,              /**< AO host clock                        */\n";
    out << "    input  wire rst_n,            /**< AO host reset, active-low            */\n\n";
    out << "    /* Control and monitors */\n";
    out << "    input  wire test_en,          /**< DFT enable to force on               */\n";
    out << "    input  wire ctrl_enable,      /**< target state: 1=on, 0=off            */\n";
    out << "    input  wire fault_clear,      /**< pulse to clear sticky fault          */\n\n";
    out << "    input  wire dep_hard_all,     /**< AND of all hard-depend ready inputs  */\n";
    out << "    input  wire dep_soft_all,     /**< AND of all soft-depend ready inputs  */\n";
    out << "    input  wire pgood,            /**< power good of this domain            */\n\n";
    out << "    /* Domain-side controls */\n";
    out << "    output reg  clk_enable,       /**< ICG enable for this domain clock     */\n";
    out << "    output reg  rst_gate_n,       /**< reset gate to synchronizer, active-low */\n";
    out << "    output reg  pwr_switch,       /**< power switch control                 */\n\n";
    out << "    /* Status */\n";
    out << "    output reg  ready,            /**< domain usable clock on reset off     */\n";
    out << "    output reg  valid,            /**< voltage stable                       */\n";
    out << "    output reg  fault             /**< sticky fault indicator               */\n";
    out << ");\n\n";

    out << "    /** Return number of bits to hold value n at least 1 */\n";
    out << "    function integer bits_for;\n";
    out << "        input integer n;\n";
    out << "        integer val, nbits;\n";
    out << "    begin\n";
    out << "        val = (n < 1) ? 1 : n;\n";
    out << "        val = val - 1;\n";
    out << "        nbits = 0;\n";
    out << "        while (val > 0) begin\n";
    out << "            nbits = nbits + 1;\n";
    out << "            val = val >> 1;\n";
    out << "        end\n";
    out << "        if (nbits < 1) nbits = 1;\n";
    out << "        bits_for = nbits;\n";
    out << "    end\n";
    out << "    endfunction\n\n";

    out << "    localparam integer MAX_ONOFF =\n";
    out << "        (SETTLE_ON_CYCLES > SETTLE_OFF_CYCLES) ? SETTLE_ON_CYCLES : "
           "SETTLE_OFF_CYCLES;\n";
    out << "    localparam integer MAX_ALL =\n";
    out << "        (MAX_ONOFF > WAIT_DEP_CYCLES) ? MAX_ONOFF : WAIT_DEP_CYCLES;\n";
    out << "    localparam integer WIDTH = bits_for(MAX_ALL);\n\n";

    out << "    /* add two light-weight states for strict ordering */\n";
    out << "    localparam [2:0] S_OFF        = 3'd0;\n";
    out << "    localparam [2:0] S_WAIT_DEP   = 3'd1;\n";
    out << "    localparam [2:0] S_TURN_ON    = 3'd2;\n";
    out << "    localparam [2:0] S_ON         = 3'd3;\n";
    out << "    localparam [2:0] S_TURN_OFF   = 3'd4;\n";
    out << "    localparam [2:0] S_FAULT      = 3'd5;\n";
    out << "    localparam [2:0] S_CLK_ON     = 3'd6;  /**< clock on, reset held        */\n";
    out << "    localparam [2:0] S_RST_ASSERT = 3'd7;  /**< reset asserted, clock on    */\n\n";

    out << "    reg [2:0] state, state_n;\n\n";

    out << "    reg [WIDTH-1:0] t_dep, t_on, t_off;\n";
    out << "    reg             ld_dep, dec_dep;\n";
    out << "    reg             ld_on,  dec_on;\n";
    out << "    reg             ld_off, dec_off;\n\n";

    out << "    /* One-cycle set for soft-timeout fault */\n";
    out << "    reg set_fault_soft;\n\n";
    out << "    /* One-cycle pulse for entering S_TURN_OFF state */\n";
    out << "    reg off_start;\n\n";

    out << "    /* 1) Sequential: state, timers, sticky fault */\n";
    out << "    always @(posedge clk or negedge rst_n) begin\n";
    out << "        if (!rst_n) begin\n";
    out << "            state <= S_OFF;\n";
    out << "            t_dep <= {WIDTH{1'b0}};\n";
    out << "            t_on  <= {WIDTH{1'b0}};\n";
    out << "            t_off <= {WIDTH{1'b0}};\n";
    out << "            fault <= 1'b0;\n";
    out << "            off_start <= 1'b0;\n";
    out << "        end else begin\n";
    out << "            state <= state_n;\n";
    out << "            off_start <= (state != S_TURN_OFF) && (state_n == S_TURN_OFF);\n\n";
    out << "            /* Load N-1; zero means no wait */\n";
    out << "            if (ld_dep)\n";
    out << "                t_dep <= (WAIT_DEP_CYCLES == 0) ? {WIDTH{1'b0}}\n";
    out << "                                               : WAIT_DEP_CYCLES - 1;\n";
    out << "            else if (dec_dep && t_dep != 0)\n";
    out << "                t_dep <= t_dep - 1'b1;\n\n";
    out << "            if (ld_on)\n";
    out << "                t_on  <= (SETTLE_ON_CYCLES == 0) ? {WIDTH{1'b0}}\n";
    out << "                                                : SETTLE_ON_CYCLES - 1;\n";
    out << "            else if (dec_on && t_on != 0)\n";
    out << "                t_on  <= t_on - 1'b1;\n\n";
    out << "            if (ld_off)\n";
    out << "                t_off <= (SETTLE_OFF_CYCLES == 0) ? {WIDTH{1'b0}}\n";
    out << "                                                 : SETTLE_OFF_CYCLES - 1;\n";
    out << "            else if (dec_off && t_off != 0)\n";
    out << "                t_off <= t_off - 1'b1;\n\n";
    out << "            /* Sticky fault from soft-timeout or entering FAULT */\n";
    out << "            if (set_fault_soft) fault <= 1'b1;\n";
    out << "            if (state_n == S_FAULT)    fault <= 1'b1;\n\n";
    out << "            /* Optional clear by software while in S_FAULT */\n";
    out << "            if (state == S_FAULT && fault_clear) fault <= 1'b0;\n";
    out << "        end\n";
    out << "    end\n\n";

    out << "    /* 2) Combinational: next state */\n";
    out << "    always @* begin\n";
    out << "        state_n        = state;\n";
    out << "        ld_dep         = 1'b0;  dec_dep  = 1'b0;\n";
    out << "        ld_on          = 1'b0;  dec_on   = 1'b0;\n";
    out << "        ld_off         = 1'b0;  dec_off  = 1'b0;\n";
    out << "        set_fault_soft = 1'b0;\n\n";
    out << "        case (state)\n";
    out << "        S_OFF: begin\n";
    out << "            if (ctrl_enable) begin\n";
    out << "                state_n = S_WAIT_DEP;\n";
    out << "                ld_dep  = 1'b1;\n";
    out << "            end\n";
    out << "        end\n\n";
    out << "        S_WAIT_DEP: begin\n";
    out << "            if (!ctrl_enable) begin\n";
    out << "                state_n = S_OFF;\n";
    out << "            end else if (dep_hard_all &&\n";
    out << "                        (dep_soft_all || (t_dep == 0))) begin\n";
    out << "                if (!dep_soft_all && (t_dep == 0))\n";
    out << "                    set_fault_soft = 1'b1; /* soft miss -> warn */\n";
    out << "                state_n = S_TURN_ON;\n";
    out << "                ld_on   = 1'b1;\n";
    out << "            end else if (!dep_hard_all && (t_dep == 0)) begin\n";
    out << "                state_n = S_FAULT;  /* hard miss -> block */\n";
    out << "                ld_dep  = 1'b1;     /* start cooldown */\n";
    out << "            end else begin\n";
    out << "                dec_dep = 1'b1;\n";
    out << "            end\n";
    out << "        end\n\n";
    out << "        S_TURN_ON: begin\n";
    out << "            /* t_on runs as window until pgood rises; once high, it must remain high "
           "until t_on==0 */\n";
    out << "            if (!ctrl_enable) begin\n";
    out << "                state_n = S_TURN_OFF;\n";
    out << "            end else if (pgood) begin\n";
    out << "                if (t_on == 0) state_n = S_CLK_ON; /* go to clock on state */\n";
    out << "                else           dec_on  = 1'b1;     /* settle countdown only when pgood "
           "*/\n";
    out << "            end else begin\n";
    out << "                if (t_on == 0) begin\n";
    out << "                    state_n = S_FAULT; /* on-timeout */\n";
    out << "                    ld_dep  = 1'b1;    /* cooldown */\n";
    out << "                end else begin\n";
    out << "                    dec_on  = 1'b1;    /* timeout countdown when !pgood */\n";
    out << "                end\n";
    out << "            end\n";
    out << "        end\n\n";
    out << "        S_ON: begin\n";
    out << "            if (!ctrl_enable) begin\n";
    out << "                state_n = S_RST_ASSERT; /* assert reset first */\n";
    out << "            end\n";
    out << "        end\n\n";
    out << "        S_TURN_OFF: begin\n";
    out << "            /* Load settle timer when first entering this state */\n";
    out << "            if (off_start) ld_off = 1'b1;\n\n";
    out << "            if (!pgood) begin\n";
    out << "                if (t_off == 0) state_n = S_OFF;\n";
    out << "                else            dec_off = 1'b1;\n";
    out << "            end else begin\n";
    out << "                if (t_off == 0) begin\n";
    out << "                    state_n = S_FAULT; /* off-timeout */\n";
    out << "                    ld_dep  = 1'b1;    /* cooldown */\n";
    out << "                end else begin\n";
    out << "                    dec_off = 1'b1;\n";
    out << "                end\n";
    out << "            end\n";
    out << "        end\n\n";
    out << "        S_FAULT: begin\n";
    out << "            if (!ctrl_enable) begin\n";
    out << "                state_n = S_OFF;\n";
    out << "            end else if (dep_hard_all && (t_dep == 0)) begin\n";
    out << "                state_n = S_WAIT_DEP; /* auto-heal after cooldown */\n";
    out << "                ld_dep  = 1'b1;\n";
    out << "            end else begin\n";
    out << "                if (t_dep != 0) dec_dep = 1'b1;\n";
    out << "                state_n = S_FAULT;\n";
    out << "            end\n";
    out << "        end\n\n";
    out << "        /* New states for clock-before-reset sequencing */\n";
    out << "        S_CLK_ON: begin\n";
    out << "            /* Single cycle to allow clock stabilization before reset release */\n";
    out << "            state_n = S_ON;\n";
    out << "        end\n\n";
    out << "        S_RST_ASSERT: begin\n";
    out << "            /* Assert reset first while clock is still on, then proceed to turn off "
           "*/\n";
    out << "            state_n = S_TURN_OFF;\n";
    out << "        end\n\n";
    out << "        default: state_n = S_FAULT;\n";
    out << "        endcase\n";
    out << "    end\n\n";

    out << "    /* 3) Combinational: outputs (Moore) */\n";
    out << "    always @* begin\n";
    out << "        clk_enable = 1'b0;\n";
    out << "        rst_gate_n = 1'b0;\n";
    out << "        pwr_switch = 1'b0;\n";
    out << "        ready      = 1'b0;\n";
    out << "        valid      = 1'b0;\n\n";
    out << "        case (state)\n";
    out << "        S_OFF: begin\n";
    out << "            /* Off: clock gated, reset asserted, switch off */\n";
    out << "        end\n";
    out << "        S_WAIT_DEP: begin\n";
    out << "            /* Waiting for dependencies with power off */\n";
    out << "        end\n";
    out << "        S_TURN_ON: begin\n";
    out << "            if (HAS_SWITCH) pwr_switch = 1'b1 /* request power */;\n";
    out << "            valid = pgood;\n";
    out << "            /* t_on counts as window; transition requires pgood==1 when t_on==0 */\n";
    out << "        end\n";
    out << "        S_ON: begin\n";
    out << "            /* Clock stays on, release reset, domain ready */\n";
    out << "            if (HAS_SWITCH) pwr_switch = 1'b1;\n";
    out << "            valid     = 1'b1;\n";
    out << "            rst_gate_n = 1'b1;  /* release reset */\n";
    out << "            clk_enable= 1'b1;  /* enable clock */\n";
    out << "            ready     = 1'b1;\n";
    out << "        end\n";
    out << "        S_TURN_OFF: begin\n";
    out << "            /* Reset asserted, clock gated, switch off, waiting for pgood drop + "
           "settle */\n";
    out << "            valid = pgood;\n";
    out << "        end\n";
    out << "        /* New states for clock-before-reset sequencing */\n";
    out << "        S_CLK_ON: begin\n";
    out << "            if (HAS_SWITCH) pwr_switch = 1'b1;\n";
    out << "            clk_enable = 1'b1;\n";
    out << "            valid = pgood;\n";
    out << "            rst_gate_n = 1'b0;  /* hold reset for one cycle before release */\n";
    out << "        end\n";
    out << "        S_RST_ASSERT: begin\n";
    out << "            if (HAS_SWITCH) pwr_switch = 1'b1;\n";
    out << "            clk_enable = 1'b1;\n";
    out << "            valid = pgood;\n";
    out << "            rst_gate_n = 1'b0;  /* assert reset before clock disable */\n";
    out << "        end\n";
    out << "        S_FAULT: begin\n";
    out << "            /* Quarantine: clock off, reset asserted, power off */\n";
    out << "        end\n";
    out << "        endcase\n\n";
    out << "        /* DFT force-on override */\n";
    out << "        if (test_en) begin\n";
    out << "            if (HAS_SWITCH) pwr_switch = 1'b1;\n";
    out << "            rst_gate_n = 1'b1;\n";
    out << "            clk_enable = 1'b1;\n";
    out << "            ready      = 1'b1;\n";
    out << "            valid      = 1'b1;\n";
    out << "        end\n";
    out << "    end\n\n";
    out << "endmodule\n";

    return code;
}

QString QSocPowerPrimitive::generateResetPipeModule()
{
    QString     code;
    QTextStream out(&code);

    out << "/* qsoc_power_rst_sync\n";
    out << " * Async assert, sync deassert reset synchronizer for power domains\n";
    out << " * Assert does not require clock, deassert requires STAGE edges on clk_dom\n";
    out << " */\n";
    out << "module qsoc_power_rst_sync #(parameter integer STAGE=4)(\n";
    out << "    input  wire clk_dom,      /**< domain clock source                   */\n";
    out << "    input  wire rst_gate_n,   /**< async assert, sync deassert           */\n";
    out << "    input  wire test_en,      /**< DFT force release                     */\n";
    out << "    output wire rst_dom_n     /**< synchronized domain reset, active-low */\n";
    out << ");\n";
    out << "    reg [STAGE-1:0] sr;\n\n";
    out << "    /* async assert on rst_gate_n low */\n";
    out << "    always @(posedge clk_dom or negedge rst_gate_n) begin\n";
    out << "        if (!rst_gate_n) begin\n";
    out << "            sr <= {STAGE{1'b0}};\n";
    out << "        end else begin\n";
    out << "            sr <= {sr[STAGE-2:0], 1'b1};\n";
    out << "        end\n";
    out << "    end\n\n";
    out << "    /* test_en overrides to release reset */\n";
    out << "    assign rst_dom_n = test_en ? 1'b1 : sr[STAGE-1];\n";
    out << "endmodule\n";

    return code;
}

bool QSocPowerPrimitive::isAODomain(const PowerDomain &domain, const YAML::Node &yamlNode)
{
    Q_UNUSED(domain);
    // AO domain: no "depend" key in YAML
    return !yamlNode["depend"];
}

bool QSocPowerPrimitive::isRootDomain(const PowerDomain &domain, const YAML::Node &yamlNode)
{
    // Root domain: has "depend" key with empty array
    return yamlNode["depend"] && yamlNode["depend"].IsSequence() && domain.depends.isEmpty();
}

QString QSocPowerPrimitive::getHardDependencySignal(const PowerDomain &domain)
{
    QStringList hardDeps;
    for (const auto &dep : domain.depends) {
        if (dep.type == "hard") {
            hardDeps << QString("rdy_%1").arg(dep.name);
        }
    }

    if (hardDeps.isEmpty()) {
        return "1'b1";
    }

    return hardDeps.join(" & ");
}

QString QSocPowerPrimitive::getSoftDependencySignal(const PowerDomain &domain)
{
    QStringList softDeps;
    for (const auto &dep : domain.depends) {
        if (dep.type == "soft") {
            softDeps << QString("rdy_%1").arg(dep.name);
        }
    }

    if (softDeps.isEmpty()) {
        return "1'b1";
    }

    return softDeps.join(" & ");
}
