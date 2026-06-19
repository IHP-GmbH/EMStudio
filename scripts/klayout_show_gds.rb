# EMStudio helper: select a top cell after KLayout opens a layout.
# Usage:
#   klayout -e -rm klayout_show_gds.rb -rd topcell=TOP file.gds

module EmstudioKlayoutShowGds
  def self.install
    return unless defined?($topcell) && $topcell && !$topcell.to_s.empty?

    top = $topcell.to_s
    app = RBA::Application.instance
    mw = app.main_window

    mw.on_view_created do
      view = mw.current_view
      next unless view

      layout = view.active_cellview.layout
      cell = layout.cell(top)
      next unless cell

      view.select_cell(cell.cell_index, 0)
      view.zoom_fit
    end
  end
end

EmstudioKlayoutShowGds.install
