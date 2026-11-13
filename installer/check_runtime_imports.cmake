# SPDX-License-Identifier: GPL-3.0-or-later
execute_process(
  COMMAND "${OBJDUMP}" --syms --section=.vitalink.fstubs.ScePafStdc "${ELF}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE symbols
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Could not inspect installer runtime imports: ${error}")
endif()

# PAF is loaded in main, after the C and C++ runtime starts.
string(REGEX MATCHALL "[ \t]F[ \t]+\\.vitalink\\.fstubs\\.ScePafStdc[^\r\n]+" imports "${symbols}")
foreach(import IN LISTS imports)
  string(REGEX MATCH "[^ \t]+$" symbol "${import}")
  if(NOT symbol MATCHES "^(sce_paf_|_ZN3paf)")
    message(FATAL_ERROR "Installer runtime symbol ${symbol} was linked to ScePaf")
  endif()
endforeach()
