file(MAKE_DIRECTORY "${PACKAGE_DIR}")
set(PACKAGE_DIR "${CMAKE_BINARY_DIR}/package")

# Determine configuration for multi- or single-config generators
if(CMAKE_CONFIGURATION_TYPES) # multi-config generator (VS, Xcode)
	set(PACKAGE_CONFIG "$<CONFIG>")
else() # single-config generator (Ninja, Makefiles)
	if(NOT CMAKE_BUILD_TYPE)
		set(PACKAGE_CONFIG "Debug")
	else()
		set(PACKAGE_CONFIG "${CMAKE_BUILD_TYPE}")
	endif()
endif()

# Path to your executable
set(TARGET_EXE "$<TARGET_FILE:NotepadNext>")

# Build the list of arguments for windeployqt
set(WINDEPLOYQT_ARGS --no-translations --no-system-d3d-compiler --no-compiler-runtime --no-opengl-sw)
if(PACKAGE_CONFIG STREQUAL "Debug")
	list(APPEND WINDEPLOYQT_ARGS --debug)
endif()
list(APPEND WINDEPLOYQT_ARGS "${PACKAGE_DIR}/NotepadNext.exe")

file(GLOB EXTRA_DLLS "${EXTRA_DLL_DIR}/*.dll")

# Ship the Chinese translations of Qt's own dialogs (save/discard message boxes
# etc.). windeployqt is run with --no-translations to keep the package small,
# so copy just the needed qtbase .qm files into package/translations.
# TranslationManager falls back to QLibraryInfo's TranslationsPath, which the
# qt.conf written by windeployqt points at ./translations next to the exe.
set(_qmake_exe "")
if(TARGET Qt6::qmake)
	get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
else()
	find_program(_qmake_exe NAMES qmake qmake6)
endif()

set(ZH_QM_EXTRA_CMDS "")
if(_qmake_exe)
	execute_process(COMMAND ${_qmake_exe} -query QT_INSTALL_TRANSLATIONS
		OUTPUT_VARIABLE QT_TRANSLATIONS_DIR
		OUTPUT_STRIP_TRAILING_WHITESPACE)
	string(REPLACE "\\" "/" QT_TRANSLATIONS_DIR "${QT_TRANSLATIONS_DIR}")

	set(ZH_QM_FILES "")
	foreach(qm qtbase_zh_CN.qm qtbase_zh_TW.qm)
		if(EXISTS "${QT_TRANSLATIONS_DIR}/${qm}")
			list(APPEND ZH_QM_FILES "${QT_TRANSLATIONS_DIR}/${qm}")
		endif()
	endforeach()

	if(ZH_QM_FILES)
		set(ZH_QM_EXTRA_CMDS
			COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}/translations"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ZH_QM_FILES} "${PACKAGE_DIR}/translations/")
	endif()
endif()

# Define the package target
add_custom_target(package
	COMMENT "Packaging NotepadNext for distribution"
	VERBATIM

	# Copy executable
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${TARGET_EXE}"
		"${PACKAGE_DIR}/NotepadNext.exe"

	# Copy LICENSE
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_SOURCE_DIR}/LICENSE"
		"${PACKAGE_DIR}/LICENSE"

	# Copy the two extra DLLs
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_SOURCE_DIR}/deploy/windows/libcrypto-1_1-x64.dll"
		"${PACKAGE_DIR}/libcrypto-1_1-x64.dll"

	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_SOURCE_DIR}/deploy/windows/libssl-1_1-x64.dll"
		"${PACKAGE_DIR}/libssl-1_1-x64.dll"

	# Run windeployqt with correct flags
	COMMAND windeployqt ${WINDEPLOYQT_ARGS}

	# Ship Chinese translations for Qt's built-in dialogs
	${ZH_QM_EXTRA_CMDS}
)

set(ZIP_FILE "${CMAKE_BINARY_DIR}/NotepadNext-v${PROJECT_VERSION}.zip")
add_custom_target(zip
	DEPENDS package
	COMMENT "Creating zip archive of NotepadNext package"
	VERBATIM
	COMMAND 7z a -tzip
		"${ZIP_FILE}"
		"${PACKAGE_DIR}/*"
		-x!libcrypto-1_1-x64.dll
		-x!libssl-1_1-x64.dll
)

set(NSIS_SCRIPT "${CMAKE_SOURCE_DIR}/installer/installer.nsi")
add_custom_target(installer
	DEPENDS package
	COMMENT "Building NSIS installer for NotepadNext"
	VERBATIM
	COMMAND makensis /V4 "${NSIS_SCRIPT}"
)
