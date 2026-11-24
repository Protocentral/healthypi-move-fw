# SPDX-License-Identifier: Apache-2.0


macro(app_set_runner_args)
  if(CONFIG_BOARD_HEALTHYPI_MOVE_NRF5340_CPUAPP)
    # Use alternative QSPI configuration file 
    board_runner_args(nrfjprog "--qspiini=${CMAKE_CURRENT_SOURCE_DIR}/qspi_w25.ini")
  endif()
endmacro()

