cmake_minimum_required(VERSION 3.24)

project(OpenTree VERSION 0.13.3 LANGUAGES CXX)

# Qt writes its CMake packages into the user package registry as well, so a MinGW build could
# pick up the MSVC WebEngine packages (or the other way around) and then try to link import
# libraries from the wrong toolchain. Only the configured kit is searched.
set(CMAKE_FIND_USE_PACKAGE_REGISTRY FALSE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Concurrent Network)
find_package(Qt6 QUIET COMPONENTS WebEngineWidgets WebChannel)

qt_standard_project_setup()

qt_add_executable(OpenTree MANUAL_FINALIZATION
    src/main.cpp
    resources/opentree.rc
    src/app/MainWindow.h
    src/app/MainWindow.cpp
    src/app/AppController.h
    src/app/AppController.cpp
    src/ui/TreePanel.h
    src/ui/TreePanel.cpp
    src/ui/DetailsPanel.h
    src/ui/DetailsPanel.cpp
    src/ui/ChartPanel.h
    src/ui/ChartPanel.cpp
    src/ui/ExtensionsPanel.h
    src/ui/ExtensionsPanel.cpp
    src/ui/GraphPanel.h
    src/ui/GraphPanel.cpp
    src/ui/HeatmapPanel.h
    src/ui/HeatmapPanel.cpp
    src/ui/DetailsTablePanel.h
    src/ui/DetailsTablePanel.cpp
    src/ui/DriveSelector.h
    src/ui/DriveSelector.cpp
    src/ui/ThemeManager.h
    src/ui/ThemeManager.cpp
    src/ui/SnapshotSettingsDialog.h
    src/ui/SnapshotSettingsDialog.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
    src/ui/SnapshotManagerDialog.h
    src/ui/SnapshotManagerDialog.cpp
    src/ui/SnapshotTimelineWidget.h
    src/ui/SnapshotTimelineWidget.cpp
    src/ui/SnapshotTrendWidget.h
    src/ui/SnapshotTrendWidget.cpp
    src/ui/TimelinePanel.h
    src/ui/TimelinePanel.cpp
    src/models/FolderTreeModel.h
    src/models/FolderTreeModel.cpp
    src/domain/ScanTypes.h
    src/services/ConfigService.h
    src/services/ConfigService.cpp
    src/services/SnapshotService.h
    src/services/SnapshotService.cpp
    src/services/ScanService.h
    src/services/ScanService.cpp
    src/services/DedupService.h
    src/services/DedupService.cpp
    src/services/VirtualTrashService.h
    src/services/VirtualTrashService.cpp
    src/services/ReportService.h
    src/services/ReportService.cpp
    src/services/PdfReportWriter.h
    src/services/PdfReportWriter.cpp
    src/services/AnalysisService.h
    src/services/AnalysisService.cpp
    src/services/UpdateChecker.h
    src/services/UpdateChecker.cpp
    src/ui/AboutDialog.h
    src/ui/AboutDialog.cpp
    src/ui/EntryActions.h
    src/ui/EntryActions.cpp
    src/ui/RobustMenuBar.h
    src/ui/RobustMenuBar.cpp
    src/ui/EverythingPromptDialog.h
    src/ui/EverythingPromptDialog.cpp
    src/utils/ProcessUtils.h
    src/utils/ProcessUtils.cpp
    src/ui/InsightsPanel.h
    src/ui/InsightsPanel.cpp
    src/ui/DuplicatesPanel.h
    src/ui/DuplicatesPanel.cpp
    src/ui/TrashPanel.h
    src/ui/TrashPanel.cpp
    src/ui/StagingReviewDialog.h
    src/ui/StagingReviewDialog.cpp
    src/ui/TableItems.h
    src/data/DatabaseManager.h
    src/data/DatabaseManager.cpp
    src/data/FolderRepository.h
    src/data/FolderRepository.cpp
    src/data/FileRepository.h
    src/data/FileRepository.cpp
    src/integrations/EverythingClient.h
    src/integrations/EverythingClient.cpp
    src/utils/Logger.h
    src/utils/Logger.cpp
    src/utils/MemoryProbe.h
    src/utils/MemoryProbe.cpp
    src/utils/FileMetadataUtils.h
    src/utils/FileMetadataUtils.cpp
    src/utils/PathUtils.h
    src/utils/PathUtils.cpp
    src/utils/SizeFormatter.h
    src/utils/SizeFormatter.cpp
    src/utils/TaskSchedulerUtils.h
    src/utils/TaskSchedulerUtils.cpp
    resources/resources.qrc
)

# The app is QWidget-only. WebEngine pulls Qt6::Qml in as a dependency, which makes
# Qt run its QML import scanner on this target; the scanner fails on a target with no
# QML sources, so skip it explicitly.
set_target_properties(OpenTree PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)

target_include_directories(OpenTree PRIVATE
    src
    third_party/include
)

target_link_libraries(OpenTree PRIVATE
    Qt6::Widgets
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Network
)

# Optional: the Graph tab needs WebEngine. Check for the imported targets rather than the
# Qt6WebEngineWidgets_FOUND variables, which are not set reliably across kits (same trap as
# Qt6::PrintSupport below), and fall back to the per-module packages if the aggregate
# find_package(Qt6 COMPONENTS ...) did not load them.
#
# WebEngine only exists in the MSVC kit here (Qt does not ship it for MinGW), and looking for it
# in a MinGW build would only find the MSVC packages, so it is skipped for other compilers.
if (MSVC)
    find_package(Qt6 QUIET COMPONENTS WebEngineWidgets WebChannel)
    if (NOT TARGET Qt6::WebEngineWidgets)
        find_package(Qt6WebEngineWidgets QUIET)
    endif()
    if (NOT TARGET Qt6::WebChannel)
        find_package(Qt6WebChannel QUIET)
    endif()
endif()

set(_opentree_have_webengine_widgets FALSE)
set(_opentree_have_webchannel FALSE)
if (TARGET Qt6::WebEngineWidgets)
    set(_opentree_have_webengine_widgets TRUE)
endif()
if (TARGET Qt6::WebChannel)
    set(_opentree_have_webchannel TRUE)
endif()

if (_opentree_have_webengine_widgets AND _opentree_have_webchannel)
    message(STATUS "OpenTree: Graph tab enabled (renderer built as a WebEngine plugin)")

    # WebEngine lives in a plugin instead of the executable. Loading Chromium commits a few
    # hundred megabytes, and most sessions never open the Graph tab: the plugin is loaded the
    # first time the tab is shown, so an unused graph costs nothing.
    add_library(opentree_graph_webengine MODULE
        src/graph/WebEngineGraphPlugin.cpp
        src/graph/IGraphView.h
    )
    target_include_directories(opentree_graph_webengine PRIVATE src)
    target_link_libraries(opentree_graph_webengine PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::WebEngineWidgets
        Qt6::WebChannel
    )
    set_target_properties(opentree_graph_webengine PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:OpenTree>"
        LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:OpenTree>"
    )
else()
    message(WARNING "OpenTree: Graph tab DISABLED (WebEngineWidgets target found: ${_opentree_have_webengine_widgets}, WebChannel target found: ${_opentree_have_webchannel}) - the tab will show a text list")
endif()

# Windows GUI subsystem: without this the MSVC build links a console binary and Windows
# opens a console window next to the app. Set before finalization so it always wins.
set_target_properties(OpenTree PROPERTIES WIN32_EXECUTABLE TRUE)
qt_finalize_executable(OpenTree)

# ---------------------------------------------------------------------------
# Core logic tests (path helpers, size formatting, dedup, analysis, reports, ledger)
# ---------------------------------------------------------------------------
find_package(Qt6 QUIET COMPONENTS Test)
if (TARGET Qt6::Test)
    enable_testing()
    qt_add_executable(OpenTreeTests MANUAL_FINALIZATION
        tests/tst_core.cpp
        resources/resources.qrc
        src/utils/PathUtils.cpp
        src/utils/SizeFormatter.cpp
        src/services/DedupService.cpp
        src/services/AnalysisService.cpp
        src/services/ReportService.cpp
        src/services/PdfReportWriter.cpp
        src/services/SnapshotService.cpp
        src/services/UpdateChecker.cpp
        src/services/VirtualTrashService.cpp
        src/integrations/EverythingClient.cpp
        src/ui/EntryActions.cpp
        src/ui/ThemeManager.cpp
        src/utils/Logger.cpp
    )
    target_include_directories(OpenTreeTests PRIVATE src third_party/include)
    target_link_libraries(OpenTreeTests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Network Qt6::Test)
    # Console subsystem so the test log is visible when run from a terminal.
    set_target_properties(OpenTreeTests PROPERTIES WIN32_EXECUTABLE FALSE)
    if (MINGW)
        target_link_options(OpenTreeTests PRIVATE -Wl,--subsystem,console)
    elseif (MSVC)
        target_link_options(OpenTreeTests PRIVATE /SUBSYSTEM:CONSOLE)
    endif()
    add_test(NAME core COMMAND OpenTreeTests)
    qt_finalize_executable(OpenTreeTests)
    message(STATUS "OpenTree: core tests enabled (OpenTreeTests)")
else()
    message(STATUS "OpenTree: core tests disabled (Qt6::Test not found)")
endif()

if (WIN32)
    target_link_libraries(OpenTree PRIVATE shell32)
    if (MINGW)
        target_link_options(OpenTree PRIVATE -mwindows)
    endif()
endif()

add_custom_command(TARGET OpenTree POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dll/Everything64.dll"
        "$<TARGET_FILE_DIR:OpenTree>/Everything64.dll"
)

if (WIN32)
    set(QT_WINDEPLOYQT_EXECUTABLE "${CMAKE_PREFIX_PATH}/bin/windeployqt.exe")
    if (EXISTS "${QT_WINDEPLOYQT_EXECUTABLE}")
        # The PDF report uses QPdfWriter (QtGui), so PrintSupport is not needed... except in
        # the WebEngine plugin: Qt6WebEngineWidgets.dll itself imports Qt6PrintSupport.dll, so
        # it must stay deployed whenever the graph renderer plugin is built. The graph also
        # needs opengl32sw.dll: QT_OPENGL=software keeps the shared context Qt builds for
        # WebEngine from dragging the GPU driver into the app process, which costs a couple of
        # hundred megabytes while the graph is open.
        set(OPENTREE_WINDEPLOYQT_EXTRA_FLAGS --no-translations)
        set(OPENTREE_WINDEPLOYQT_BINARIES "$<TARGET_FILE:OpenTree>")
        if (NOT TARGET Qt6::WebEngineWidgets)
            list(APPEND OPENTREE_WINDEPLOYQT_EXTRA_FLAGS --no-printsupport --no-opengl-sw)
        else()
            # Pass the plugin too so windeployqt deploys the WebEngine runtime the plugin needs
            # (scanning only OpenTree.exe would miss it now that the app does not link WebEngine).
            list(APPEND OPENTREE_WINDEPLOYQT_BINARIES "$<TARGET_FILE:opentree_graph_webengine>")
        endif()

        add_custom_command(TARGET OpenTree POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXECUTABLE}"
                --dir "$<TARGET_FILE_DIR:OpenTree>"
                ${OPENTREE_WINDEPLOYQT_EXTRA_FLAGS}
                ${OPENTREE_WINDEPLOYQT_BINARIES}
            COMMENT "Deploying Qt runtime dependencies"
        )
    endif()
endif()
