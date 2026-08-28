# Desktop integration: the launcher entry, the software-centre metadata and the
# application icon.
#
# SPDX-License-Identifier: GPL-2.0-or-later

install(FILES de.synthelicz.Norn.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES de.synthelicz.Norn.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES de.synthelicz.Norn.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
