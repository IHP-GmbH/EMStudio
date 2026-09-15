# EMStudio helper: after opening a GDS, optionally attach technology and select top cell.
# Usage:
#   klayout -e -rm klayout_show_gds.rb -rd tech=sg13g2 -rd topcell=TOP file.gds
#
# Plain GDS files often open without a technology. PDK PyCells are only placeable
# when the layout's technology is set (same as File → New Layout → choose tech).
# Technology name comes from EMStudio Preferences → KLAYOUT_TECH (-rd tech=...).

module EmstudioKlayoutShowGds
  def self.install
    top = (defined?($topcell) && $topcell) ? $topcell.to_s : ""
    tech = (defined?($tech) && $tech) ? $tech.to_s : ""

    app = RBA::Application.instance
    mw = app.main_window

    mw.on_view_created do
      view = mw.current_view
      next unless view

      cv = view.active_cellview
      next unless cv

      # Attach tech from -rd tech=... so Library Browser exposes PDK PCells for this layout.
      begin
        cv.technology = tech unless tech.empty?
      rescue
        # Technology may be missing if PDK was not loaded; keep going.
      end

      next if top.empty?

      layout = cv.layout
      cell = layout.cell(top)
      next unless cell

      view.select_cell(cell.cell_index, 0)
      view.zoom_fit
    end
  end
end

EmstudioKlayoutShowGds.install
