add_subdirectory(player)
add_subdirectory(coach)
add_subdirectory(trainer)

# generate shell scripts and copy them to the binary direcotry as an executable script
configure_file(
  ${PROJECT_SOURCE_DIR}/src/start.sh.in
  ${PROJECT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/start.sh
  @ONLY
  )
file(
  COPY ${PROJECT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/start.sh
  DESTINATION ${PROJECT_BINARY_DIR}/bin
  FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
  )

configure_file(
  ${PROJECT_SOURCE_DIR}/src/keepaway.sh.in
  ${PROJECT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/keepaway.sh
  @ONLY
  )
file(
  COPY ${PROJECT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/keepaway.sh
  DESTINATION ${PROJECT_BINARY_DIR}/bin
  FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
  )

configure_file(
  ${PROJECT_SOURCE_DIR}/src/train.sh.in
  ${PROJECT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/train.sh
  @ONLY
  )
file(
  COPY ${PROJECT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/train.sh
  DESTINATION ${PROJECT_BINARY_DIR}/bin
  FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
  )

# copy other files to the binary direcotry
file(COPY
  formations-dt formations-keeper formations-taker player.conf coach.conf start-debug.sh start-offline.sh unmark_dnn_weights.txt robotech_logo.xpm
  # DESTINATION ${PROJECT_BINARY_DIR}/src/
  DESTINATION ${PROJECT_BINARY_DIR}/bin
  )

# file(COPY) solo corre al RECONFIGURAR cmake, no en cada make: build/bin se
# quedaba con formaciones/pesos viejos aunque src/ cambiara (3 semanas jugando
# con formations-dt de junio-12 corruptas). Este target re-sincroniza en CADA
# build. Incluye field_eval_weights.txt, que faltaba en la lista de arriba.
add_custom_target(sync_runtime_data ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${PROJECT_SOURCE_DIR}/src/formations-dt ${PROJECT_BINARY_DIR}/bin/formations-dt
  # field_eval_weights.txt EXCLUIDO a propósito (2026-07-05): el A/B N=30 midió
  # −0.8 GA/partido con el field_eval DNN apagado y la entrega va OFF — el sync
  # lo estaba RESUCITANDO en build/bin en cada make. src/ lo conserva solo para
  # el track de reentrenamiento; para reactivarlo, copiarlo a bin/ a mano.
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${PROJECT_SOURCE_DIR}/src/unmark_dnn_weights.txt
          ${PROJECT_SOURCE_DIR}/src/helios_golie_origin
          ${PROJECT_SOURCE_DIR}/src/rival_tuning.conf
          ${PROJECT_BINARY_DIR}/bin/
  COMMENT "Sincronizando formations-dt, pesos DNN y rival_tuning.conf a bin/"
  )
