// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2023-2025 Huang Rui <vowstar@gmail.com>

#ifndef QSOCMODULEMANAGER_H
#define QSOCMODULEMANAGER_H

#include "common/qllmservice.h"
#include "common/qslangdriver.h"
#include "common/qsocbusmanager.h"
#include "common/qsocprojectmanager.h"

#include <QObject>
#include <QRegularExpression>

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

using json = nlohmann::json;

/**
 * @brief The QSocModuleManager class.
 * @details This class is used to manage the module library files.
 */
class QSocModuleManager : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Constructor.
     * @details This constructor will create an instance of this object.
     * @param[in] parent parent object.
     * @param[in] projectManager project manager.
     * @param[in] busManager bus manager.
     * @param[in] llmService LLM service.
     */
    explicit QSocModuleManager(
        QObject            *parent         = nullptr,
        QSocProjectManager *projectManager = nullptr,
        QSocBusManager     *busManager     = nullptr,
        QLLMService        *llmService     = nullptr);

    /**
     * @brief Destructor for QSlangDriver.
     * @details This destructor will free all the allocated resources.
     */
    ~QSocModuleManager() override;

public slots:
    /**
     * @brief Set the project manager.
     * @details Assigns a new project manager to this object. The project
     *          manager is used for managing various project-related
     *          functionalities.
     * @param projectManager Pointer to the new project manager.
     */
    void setProjectManager(QSocProjectManager *projectManager);

    /**
     * @brief Set the bus manager.
     * @details Assigns a new bus manager to this object. The bus manager
     *          is used for managing bus-related functionalities.
     * @param busManager Pointer to the new bus manager.
     */
    void setBusManager(QSocBusManager *busManager);

    /**
     * @brief Set the LLM service.
     * @details Assigns a new LLM service to this object.
     * @param llmService Pointer to the new LLM service.
     */
    void setLLMService(QLLMService *llmService);

    /**
     * @brief Get the project manager.
     * @details Retrieves the currently assigned project manager. This manager
     *          is responsible for handling various aspects of the project.
     * @return QSocProjectManager * Pointer to the current project manager.
     */
    QSocProjectManager *getProjectManager();

    /**
     * @brief Get the LLM service.
     * @details Retrieves the currently assigned LLM service.
     * @return QLLMService * Pointer to the current LLM service.
     */
    QLLMService *getLLMService();

    /**
     * @brief Get the bus manager.
     * @details Retrieves the currently assigned bus manager.
     * @return QSocBusManager * Pointer to the current bus manager.
     */
    QSocBusManager *getBusManager();

    /**
     * @brief Check if module path is valid.
     * @details Verifies the validity of the module path set in the project
     *          manager. Checks if the path exists and is a directory.
     * @retval true Module path is valid.
     * @retval false Module path is invalid, or projectManager is nullptr.
     */
    bool isModulePathValid();

    /**
     * @brief Reset the module data in memory.
     * @details Clears the libraryMap and moduleData members, providing a fresh
     *          environment for the next load operation. This method does not
     *          physically delete any files on disk, only clearing the in-memory
     *          representation of module data.
     */
    void resetModuleData();

    /**
     * @brief Import verilog files from file list.
     * @details This function will import verilog files from file list, and
     *          generate the module library file.
     *          If libraryName is empty, the first matching verilog module
     *          name is automatically selected and converted into lowercase as
     *          the module library name.
     *          If moduleNameRegex is empty, the first matching verilog module
     *          is automatically selected for import.
     * @param libraryName The basename of the module library file without ext.
     * @param moduleNameRegex Regular expression to match the module name.
     * @param fileListPath The path of the verilog file list.
     * @param filePathList The list of verilog files.
     * @param macroDefines The list of macro definitions in KEY=VALUE format.
     * @param macroUndefines The list of macro names to undefine.
     * @retval true Import successfully.
     * @retval false Import failed.
     */
    bool importFromFileList(
        const QString            &libraryName,
        const QRegularExpression &moduleNameRegex,
        const QString            &fileListPath,
        const QStringList        &filePathList,
        const QStringList        &macroDefines   = QStringList(),
        const QStringList        &macroUndefines = QStringList());

    /**
     * @brief Get the Module Yaml object.
     * @details This function will convert the module AST json object to YAML
     *          object. This function relies on projectManager to be valid.
     * @param moduleAst The module AST json object.
     * @return YAML::Node The module YAML object.
     */
    YAML::Node getModuleYaml(const json &moduleAst);

    /**
     * @brief Get the Module Yaml object.
     * @details This function will get the YAML node for a specific module from
     *          moduleData. The module must exist in moduleData (loaded using
     *          one of the load() functions).
     * @param moduleName The name of the module.
     * @return YAML::Node The module YAML object. Returns an empty node if
     *         module does not exist.
     */
    YAML::Node getModuleYaml(const QString &moduleName);

    /**
     * @brief Save the library YAML object to library file.
     * @details This function will save the library YAML object to library file.
     * @param libraryName The basename of the library file without ext.
     * @param libraryYaml The library YAML object.
     * @retval true Save successfully.
     * @retval false Save failed.
     */
    bool saveLibraryYaml(const QString &libraryName, const YAML::Node &libraryYaml);

    /**
     * @brief Check if the library file exists in the filesystem.
     * @details This function checks if a library file with the given basename
     *          exists in the module directory. It is used to verify the
     *          existence of library files before processing them.
     * @param libraryName The basename of the library file, without the
     *        ".soc_mod" extension.
     * @retval true Library file exists in the module directory.
     * @retval false Library file does not exist in the module directory.
     */
    bool isLibraryFileExist(const QString &libraryName);

    /**
     * @brief Check if a library is loaded in memory.
     * @details Checks whether the specified library name exists in the
     *          libraryMap. This function verifies if a library has been
     *          loaded into memory using one of the load() functions,
     *          rather than checking for the file's existence on disk.
     * @param libraryName Name of the library to check
     * @retval true Library exists in libraryMap
     * @retval false Library does not exist in libraryMap
     */
    bool isLibraryExist(const QString &libraryName);

    /**
     * @brief Get list of library basenames.
     * @details Retrieves basenames of ".soc_mod" files in directory defined by
     *          `modulePath`, excluding the ".soc_mod" extension. Scans the
     *          module directory and compiles a list of relevant basenames.
     *          Useful for processing or iterating over library files. This
     *          function relies on projectManager to be valid.
     * @param libraryNameRegex Regular expression to match file basenames,
     *        default is ".*".
     * @return QStringList of basenames for all ".soc_mod" files in the module.
     *         directory, excluding the ".soc_mod" extension.
     */
    QStringList listLibrary(const QRegularExpression &libraryNameRegex = QRegularExpression(".*"));

    /**
     * @brief Load a specific library by basename.
     * @details Loads library specified by `libraryName` from the module
     *          directory. Useful for retrieving individual library files.
     *          Relies on a valid projectManager and existence of the library
     *          file.
     * @param libraryName Basename of module file to load, without ".soc_mod"
     *        extension.
     * @retval true Library is successfully loaded.
     * @retval false Loading fails or file does not exist.
     */
    bool load(const QString &libraryName);

    /**
     * @brief Load libraries matching a regex pattern.
     * @details Loads library files matching `libraryNameRegex` in the module
     *          directory. Ideal for batch processing or retrieving libraries
     *          based on naming patterns. Requires projectManager to be valid.
     * @param libraryNameRegex Regex to match file basenames, defaults to
     *        ".*", matching all libraries.
     * @retval true All matching libraries are loaded.
     * @retval false Loading any matching libraries fails.
     */
    bool load(const QRegularExpression &libraryNameRegex = QRegularExpression(".*"));

    /**
     * @brief Load multiple libraries by a list of basenames.
     * @details Loads multiple libraries specified in `libraryNameList` from the
     *          module directory. Useful for loading a specific set of
     *          library files. Requires a valid projectManager and checks the
     *          existence of each library file.
     * @param libraryNameList List of library file basenames to load, without
     *        ".soc_mod" extensions.
     * @retval true All specified libraries are successfully loaded.
     * @retval false Loading fails for any of the specified libraries.
     */
    bool load(const QStringList &libraryNameList);

    /**
     * @brief Save library data associated with a specific basename.
     * @details Serializes and saves the module data related to the given
     *          `libraryName`. It locates the corresponding modules in
     *          `moduleData` using `libraryMap`, then serializes them into YAML
     *          format. The result is saved to a file with the same basename,
     *          appending the ".soc_mod" extension. Existing files are
     *          overwritten. This function requires a valid projectManager.
     * @param libraryName The basename of the module, excluding extension.
     * @retval true on successful serialization and saving.
     * @retval false on failure to serialize or save.
     */
    bool save(const QString &libraryName);

    /**
     * @brief Save multiple libraries matching a regex pattern.
     * @details Iterates through `libraryMap` to find libraries matching the
     *          provided regex pattern. Each matching library is serialized and
     *          saved individually in YAML format. Files are named after the
     *          library basenames with the ".soc_mod" extension. Existing files
     *          are overwritten. This function requires a valid projectManager.
     * @param libraryNameRegex Regular expression to filter library basenames.
     * @retval true if all matching libraries are successfully saved.
     * @retval false if saving any matching module fails.
     */
    bool save(const QRegularExpression &libraryNameRegex = QRegularExpression(".*"));

    /**
     * @brief Save multiple libraries by a list of basenames.
     * @details Serializes and saves module data related to each `libraryName`
     *          in `libraryNameList`. It locates corresponding modules in
     *          `moduleData` using `libraryMap`, then serializes them into YAML
     *          format. Results are saved to files named after each basename,
     *          appending ".soc_mod". Existing files are overwritten. Requires
     *          a valid projectManager.
     * @param libraryNameList List of library basenames to save, excluding
     *        extensions.
     * @retval true All specified libraries are successfully saved.
     * @retval false Saving fails for any of the specified libraries.
     */
    bool save(const QStringList &libraryNameList);

    /**
     * @brief Remove a specific library by basename.
     * @details Removes the library file identified by `libraryName` from
     *          the library directory. This method is useful for deleting
     *          individual library files. It requires a valid projectManager
     *          and checks if the library file exists.
     * @param libraryName Basename of the library file to remove, without
     *        the ".soc_mod" extension.
     * @retval true The library file is successfully removed.
     * @retval false Removal fails or the file does not exist.
     */
    bool remove(const QString &libraryName);

    /**
     * @brief Remove libraries matching a regex pattern.
     * @details Removes all library files from the module directory that
     *          match `libraryNameRegex`. This method is ideal for batch
     *          removal of libraries based on naming patterns. It relies on
     *          a valid projectManager to execute.
     * @param libraryNameRegex Regex to match file basenames. Defaults
     *        to ".*", which matches all module files.
     * @retval true if all matching libraries are successfully removed.
     * @retval false if removal of any matching libraries fails.
     */
    bool remove(const QRegularExpression &libraryNameRegex);

    /**
     * @brief Remove multiple libraries by a list of basenames.
     * @details Removes multiple library files specified in `libraryNameList`
     *          from the module directory. Useful for deleting a specific set of
     *          module files. Requires a valid projectManager and checks each
     *          module file's existence.
     * @param libraryNameList List of module file basenames to remove, without
     *        ".soc_mod" extensions.
     * @retval true All specified libraries are successfully removed.
     * @retval false Removal fails for any of the specified libraries.
     */
    bool remove(const QStringList &libraryNameList);

    /**
     * @brief Check if a module exists in moduleData
     * @details Checks whether the specified module name exists in the
     *          moduleData YAML node. This function requires that the module
     *          library has been loaded using one of the load() functions
     *          before checking.
     * @param moduleName Name of the module to check
     * @retval true Module exists in moduleData
     * @retval false Module does not exist or library not loaded
     */
    bool isModuleExist(const QString &moduleName);

    /**
     * @brief Check if modules matching a regex pattern exist in moduleData
     * @details Checks whether any module name matching the specified regular expression
     *          exists in the moduleData YAML node. This function requires that the module
     *          libraries have been loaded using one of the load() functions before checking.
     * @param moduleNameRegex Regular expression to match module names
     * @retval true At least one matching module exists in moduleData
     * @retval false No matching modules exist or libraries not loaded
     */
    bool isModuleExist(const QRegularExpression &moduleNameRegex);

    /**
     * @brief Get the library name for a given module
     * @details Retrieves the library name associated with the specified module
     *          from moduleData. The module library must be loaded using one of
     *          the load() functions before calling this function. Returns an
     *          empty string if the module is not found or library not loaded.
     * @param moduleName Name of the module
     * @return Library name if module exists, empty string otherwise
     */
    QString getModuleLibrary(const QString &moduleName);

    /**
     * @brief Get list of module names matching a regex pattern.
     * @details Retrieves module names from the `moduleData` YAML node that
     *          match the provided `moduleNameRegex`. This function scans
     *          the module library and compiles a list of module names. Useful
     *          for processing or iterating over project libraries. It relies on
     *          the validity of the `moduleData` YAML node. This function
     *          requires a valid projectManager.
     * @param moduleNameRegex Regular expression to match module names,
     *        default is ".*".
     * @return QStringList of module names matching the regex in the module
     *         library.
     */
    QStringList listModule(const QRegularExpression &moduleNameRegex = QRegularExpression(".*"));

    /**
     * @brief Retrieve YAML nodes for modules matching the regex.
     * @details Fetches and returns YAML nodes for all modules whose names
     *          match the provided regular expression. This allows for
     *          querying multiple modules at once based on name patterns.
     *          The returned YAML node contains a map where each key is a
     *          matching module name and its value is the corresponding
     *          module's YAML data. Returns an empty node if no matches
     *          are found.
     * @param moduleNameRegex Regex used to filter the module names.
     *        Default is ".*", which matches all modules.
     * @return YAML::Node A map of YAML nodes, where each key is a module
     *                    name and its value is the module's YAML data.
     *                    Returns an empty node if no matches are found.
     */
    YAML::Node getModuleYamls(const QRegularExpression &moduleNameRegex = QRegularExpression(".*"));

    /**
     * @brief Update existing module's YAML data and save to its library file
     * @details Updates the YAML data for an existing module in moduleData and
     *          saves the changes to the associated library file. The module
     *          must exist in a loaded library (using load()) before updating.
     *          This function will merge the new YAML data with existing data
     *          and save the updated library file.
     * @param moduleName Name of the existing module
     * @param moduleYaml New YAML node containing module data
     * @retval true Module successfully updated and saved
     * @retval false Update failed or module/library not loaded
     */
    bool updateModuleYaml(const QString &moduleName, const YAML::Node &moduleYaml);

    /**
     * @brief Remove modules matching regex from module library.
     * @details Removes modules that match `moduleNameRegex` from moduleData,
     *          updating libraryMap accordingly. It saves libraries with
     *          remaining module associations and removes files with no
     *          associations. Requires a valid projectManager for execution.
     * @param moduleNameRegex Regex to filter module names for removal.
     * @retval true All matching modules are successfully processed.
     * @retval false Errors occur during module removal or module saving.
     */
    bool removeModule(const QRegularExpression &moduleNameRegex);

    /**
     * @brief Add a bus interface to a module.
     * @details This function adds a bus interface to a specified module by
     *          updating the module's YAML data. It first loads the bus data
     *          from the bus library, then adds the bus interface to the
     *          module's YAML with the specified port name and mode.
     * @param moduleName Name of the module to add the bus interface to.
     * @param busName Name of the bus to add.
     * @param busMode Mode of the bus (e.g., "master", "slave").
     * @param busInterface Interface name of the bus.
     * @retval true Bus interface successfully added.
     * @retval false Failed to add bus interface.
     */
    bool addModuleBus(
        const QString &moduleName,
        const QString &busName,
        const QString &busMode,
        const QString &busInterface);

    /**
     * @brief Add a bus interface to a module using LLM API for signal matching.
     * @details This method uses a large language model to match bus signals to module ports.
     * @param moduleName Name of the module.
     * @param busName Name of the bus.
     * @param busMode Mode of the bus (e.g., "master", "slave").
     * @param busInterface Interface name of the bus.
     * @retval true Bus interface successfully added.
     * @retval false Failed to add bus interface.
     */
    bool addModuleBusWithLLM(
        const QString &moduleName,
        const QString &busName,
        const QString &busMode,
        const QString &busInterface);
    /**
     * @brief Remove bus interfaces from a module.
     * @details This function removes bus interfaces that match the given regex
     *          from the specified module by updating the module's YAML data.
     *          If no bus interfaces match the regex, no changes are made.
     * @param moduleName Name of the module to remove bus interfaces from.
     * @param busInterfaceRegex Regex to match bus interfaces for removal.
     * @retval true Bus interfaces successfully removed or none matched.
     * @retval false Failed to remove bus interfaces.
     */
    bool removeModuleBus(const QString &moduleName, const QRegularExpression &busInterfaceRegex);

    /**
     * @brief List all bus interfaces in a module.
     * @details Returns a list of all bus interface names defined in the
     *          specified module's YAML data. If the module has no bus
     *          interfaces defined, returns an empty list.
     * @param moduleName Name of the module to list bus interfaces from.
     * @param busInterfaceRegex Regex to match bus interfaces to list.
     * @return QStringList List of bus interface names in the module.
     */
    QStringList listModuleBus(
        const QString            &moduleName,
        const QRegularExpression &busInterfaceRegex = QRegularExpression(".*"));

    /**
     * @brief Show detailed information about bus interfaces in a module.
     * @details Returns a YAML node containing detailed information about bus
     *          interfaces that match the given regex in the specified module.
     *          This includes interface names, modes, and signal mappings.
     * @param moduleName Name of the module to show bus interfaces from.
     * @param busInterfaceRegex Regex to match bus interfaces to show.
     * @return YAML::Node YAML node containing matching bus interface details.
     */
    YAML::Node showModuleBus(const QString &moduleName, const QRegularExpression &busInterfaceRegex);

    /**
     * @brief Format LLM response JSON into a Markdown table for bus interface analysis
     * @details Converts JSON containing potential bus interface groupings into a
     *          formatted Markdown table with columns for interface properties
     *
     * @param jsonResponse The JSON string from LLM containing bus interface analysis
     * @return Formatted Markdown table as a string
     */
    QString formatModuleBusJsonToMarkdownTable(const QString &jsonResponse);

    /**
     * @brief Explain module bus interfaces using LLM analysis.
     * @details This function will use the LLM service to analyze the module ports
     *          and bus signals to identify potential bus interfaces. The
     *          explanation is returned as a Markdown-formatted string.
     * @param moduleName The name of the module.
     * @param busName The name of the bus.
     * @param explanation The explanation string to be filled.
     * @retval true Explanation successful.
     * @retval false Explanation failed.
     */
    bool explainModuleBusWithLLM(
        const QString &moduleName, const QString &busName, QString &explanation);

private:
    /* Project manager. */
    QSocProjectManager *projectManager = nullptr;

    /* Bus manager. */
    QSocBusManager *busManager = nullptr;

    /* LLM service. */
    QLLMService *llmService = nullptr;

    /* Slang driver. */
    QSlangDriver *slangDriver = nullptr;

    /* This QMap, libraryMap, maps library names to sets of module names.
       Each key in the map is a library name (QString).
       The corresponding value is a QSet<QString> containing the names
       of all modules that are part of that library.
       - key: QString libraryName
       - value: QSet<QString> moduleNameSet
       This structure ensures that each library name is associated with
       a unique set of module names, allowing efficient retrieval and
       management of modules within each library. */
    QMap<QString, QSet<QString>> libraryMap;

    /* Module library YAML node. */
    YAML::Node moduleData;

    /**
     * @brief Merge two YAML nodes.
     * @details This function will merge two YAML nodes. It returns a new map
     *          resultYaml which is a merge of fromYaml into toYaml.
     *          Values from fromYaml will replace identically keyed non-map
     *          values from toYaml in the resultYaml map.
     *          The values ​​in toYaml and fromYaml will not be modified.
     * @param toYaml The destination YAML node.
     * @param fromYaml The source YAML node.
     * @return YAML::Node The merged YAML node which is a merge of fromYaml into
     *                    toYaml.
     */
    YAML::Node mergeNodes(const YAML::Node &toYaml, const YAML::Node &fromYaml);

    /**
     * @brief Adds a module name to the library map.
     * @details This function adds a given module name to the set of modules
     *          associated with a specified library in the libraryMap.
     *          If the library does not already exist in the map, it creates
     *          a new entry with an empty set of modules and then adds the
     *          module name. If the library exists, it simply adds the module
     *          name to the existing set.
     * @param libraryName The name of the library.
     * @param moduleName The name of the module to add to the library.
     */
    void libraryMapAdd(const QString &libraryName, const QString &moduleName);

    /**
     * @brief Removes a module from a library in the library map.
     * @details This function removes the specified module from the set
     *          associated with the given library name in the library map. If
     *          the module is the only one in the set, it also removes the
     *          entire library entry from the map. Does nothing if the library
     *          or module does not exist.
     * @param libraryName The name of the library to be modified.
     * @param moduleName The name of the module to be removed.
     */
    void libraryMapRemove(const QString &libraryName, const QString &moduleName);

signals:
};

#endif // QSOCMODULEMANAGER_H
