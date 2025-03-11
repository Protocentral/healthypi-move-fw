/**
 * @file lv_chart.h
 *
 */

#ifndef HPI_LV_CHART_H
#define HPI_LV_CHART_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "lvgl.h"

//#if LV_USE_CHART != 0

/*********************
 *      DEFINES
 *********************/

/**Default value of points. Can be used to not draw a point*/
#if LV_USE_LARGE_COORD
#define LV_CHART_POINT_NONE (INT32_MAX)
#else
#define LV_CHART_POINT_NONE (INT16_MAX)
#endif
LV_EXPORT_CONST_INT(LV_CHART_POINT_NONE);

/**********************
 *      TYPEDEFS
 **********************/

extern const lv_obj_class_t lv_chart_class;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Create a chart object
 * @param parent    pointer to an object, it will be the parent of the new chart
 * @return          pointer to the created chart
 */
lv_obj_t * hpi_lv_chart_create(lv_obj_t * parent);

/**
 * Set a new type for a chart
 * @param obj       pointer to a chart object
 * @param type      new type of the chart (from 'lv_chart_type_t' enum)
 */
void hpi_lv_chart_set_type(lv_obj_t * obj, lv_chart_type_t type);
/**
 * Set the number of points on a data line on a chart
 * @param obj       pointer to a chart object
 * @param cnt       new number of points on the data lines
 */
void hpi_lv_chart_set_point_count(lv_obj_t * obj, uint16_t cnt);

/**
 * Set the minimal and maximal y values on an axis
 * @param obj       pointer to a chart object
 * @param axis      `LV_CHART_AXIS_PRIMARY_Y` or `LV_CHART_AXIS_SECONDARY_Y`
 * @param min       minimum value of the y axis
 * @param max       maximum value of the y axis
 */
void hpi_lv_chart_set_range(lv_obj_t * obj, lv_chart_axis_t axis, lv_coord_t min, lv_coord_t max);

/**
 * Set update mode of the chart object. Affects
 * @param obj       pointer to a chart object
 * @param mode      the update mode
 */
void hpi_lv_chart_set_update_mode(lv_obj_t * obj, lv_chart_update_mode_t update_mode);

/**
 * Set the number of horizontal and vertical division lines
 * @param obj       pointer to a chart object
 * @param hdiv      number of horizontal division lines
 * @param vdiv      number of vertical division lines
 */
void hpi_lv_chart_set_div_line_count(lv_obj_t * obj, uint8_t hdiv, uint8_t vdiv);

/**
 * Zoom into the chart in X direction
 * @param obj       pointer to a chart object
 * @param zoom_x    zoom in x direction. LV_ZOOM_NONE or 256 for no zoom, 512 double zoom
 */
void hpi_lv_chart_set_zoom_x(lv_obj_t * obj, uint16_t zoom_x);

/**
 * Zoom into the chart in Y direction
 * @param obj       pointer to a chart object
 * @param zoom_y    zoom in y direction. LV_ZOOM_NONE or 256 for no zoom, 512 double zoom
 */
void hpi_lv_chart_set_zoom_y(lv_obj_t * obj, uint16_t zoom_y);

/**
 * Get X zoom of a chart
 * @param obj       pointer to a chart object
 * @return          the X zoom value
 */
uint16_t hpi_lv_chart_get_zoom_x(const lv_obj_t * obj);

/**
 * Get Y zoom of a chart
 * @param obj       pointer to a chart object
 * @return          the Y zoom value
 */
uint16_t hpi_lv_chart_get_zoom_y(const lv_obj_t * obj);

/**
 * Set the number of tick lines on an axis
 * @param obj           pointer to a chart object
 * @param axis          an axis which ticks count should be set
 * @param major_len     length of major ticks
 * @param minor_len     length of minor ticks
 * @param major_cnt     number of major ticks on the axis
 * @param minor_cnt     number of minor ticks between two major ticks
 * @param label_en      true: enable label drawing on major ticks
 * @param draw_size     extra size required to draw the tick and labels
 *                      (start with 20 px and increase if the ticks/labels are clipped)
 */
void hpi_lv_chart_set_axis_tick(lv_obj_t * obj, lv_chart_axis_t axis, lv_coord_t major_len, lv_coord_t minor_len,
                            lv_coord_t major_cnt, lv_coord_t minor_cnt, bool label_en, lv_coord_t draw_size);

/**
 * Get the type of a chart
 * @param obj       pointer to chart object
 * @return          type of the chart (from 'lv_chart_t' enum)
 */
lv_chart_type_t hpi_lv_chart_get_type(const lv_obj_t * obj);

/**
 * Get the data point number per data line on chart
 * @param chart     pointer to chart object
 * @return          point number on each data line
 */
uint16_t hpi_lv_chart_get_point_count(const lv_obj_t * obj);

/**
 * Get the current index of the x-axis start point in the data array
 * @param chart     pointer to a chart object
 * @param ser       pointer to a data series on 'chart'
 * @return          the index of the current x start point in the data array
 */
uint16_t hpi_lv_chart_get_x_start_point(const lv_obj_t * obj, lv_chart_series_t * ser);

/**
 * Get the position of a point to the chart.
 * @param chart     pointer to a chart object
 * @param ser       pointer to series
 * @param id        the index.
 * @param p_out     store the result position here
 */
void hpi_lv_chart_get_point_pos_by_id(lv_obj_t * obj, lv_chart_series_t * ser, uint16_t id, lv_point_t * p_out);

/**
 * Refresh a chart if its data line has changed
 * @param   chart pointer to chart object
 */
void hpi_lv_chart_refresh(lv_obj_t * obj);

/*======================
 * Series
 *=====================*/

/**
 * Allocate and add a data series to the chart
 * @param obj       pointer to a chart object
 * @param color     color of the data series
 * @param axis      the y axis to which the series should be attached (::LV_CHART_AXIS_PRIMARY_Y or ::LV_CHART_AXIS_SECONDARY_Y)
 * @return          pointer to the allocated data series
 */
lv_chart_series_t * hpi_lv_chart_add_series(lv_obj_t * obj, lv_color_t color, lv_chart_axis_t axis);

/**
 * Deallocate and remove a data series from a chart
 * @param chart     pointer to a chart object
 * @param series    pointer to a data series on 'chart'
 */
void hpi_lv_chart_remove_series(lv_obj_t * obj, lv_chart_series_t * series);

/**
 * Hide/Unhide a single series of a chart.
 * @param obj       pointer to a chart object.
 * @param series    pointer to a series object
 * @param hide      true: hide the series
 */
void hpi_lv_chart_hide_series(lv_obj_t * chart, lv_chart_series_t * series, bool hide);

/**
 * Change the color of a series
 * @param obj       pointer to a chart object.
 * @param series    pointer to a series object
 * @param color     the new color of the series
 */
void hpi_lv_chart_set_series_color(lv_obj_t * chart, lv_chart_series_t * series, lv_color_t color);

/**
 * Set the index of the x-axis start point in the data array.
 * This point will be considers the first (left) point and the other points will be drawn after it.
 * @param obj       pointer to a chart object
 * @param ser       pointer to a data series on 'chart'
 * @param id        the index of the x point in the data array
 */
void hpi_lv_chart_set_x_start_point(lv_obj_t * obj, lv_chart_series_t * ser, uint16_t id);

/**
 * Get the next series.
 * @param chart     pointer to a chart
 * @param ser      the previous series or NULL to get the first
 * @return          the next series or NULL if there is no more.
 */
lv_chart_series_t * hpi_lv_chart_get_series_next(const lv_obj_t * chart, const lv_chart_series_t * ser);

/*=====================
 * Cursor
 *====================*/

/**
 * Add a cursor with a given color
 * @param obj       pointer to chart object
 * @param color     color of the cursor
 * @param dir       direction of the cursor. `LV_DIR_RIGHT/LEFT/TOP/DOWN/HOR/VER/ALL`. OR-ed values are possible
 * @return          pointer to the created cursor
 */
lv_chart_cursor_t  * hpi_lv_chart_add_cursor(lv_obj_t * obj, lv_color_t color, lv_dir_t dir);

/**
 * Set the coordinate of the cursor with respect to the paddings
 * @param obj       pointer to a chart object
 * @param cursor    pointer to the cursor
 * @param pos       the new coordinate of cursor relative to the chart
 */
void hpi_lv_chart_set_cursor_pos(lv_obj_t * chart, lv_chart_cursor_t * cursor, lv_point_t * pos);

/**
 * Stick the cursor to a point
 * @param obj       pointer to a chart object
 * @param cursor    pointer to the cursor
 * @param ser       pointer to a series
 * @param point_id  the point's index or `LV_CHART_POINT_NONE` to not assign to any points.
 */
void hpi_lv_chart_set_cursor_point(lv_obj_t * chart, lv_chart_cursor_t * cursor, lv_chart_series_t * ser,
                               uint16_t point_id);

/**
 * Get the coordinate of the cursor with respect to the paddings
 * @param obj       pointer to a chart object
 * @param cursor    pointer to cursor
 * @return          coordinate of the cursor as lv_point_t
 */
lv_point_t hpi_lv_chart_get_cursor_point(lv_obj_t * chart, lv_chart_cursor_t * cursor);

/*=====================
 * Set/Get value(s)
 *====================*/

/**
 * Initialize all data points of a series with a value
 * @param obj       pointer to chart object
 * @param ser       pointer to a data series on 'chart'
 * @param value     the new value for all points. `LV_CHART_POINT_NONE` can be used to hide the points.
 */
void hpi_lv_chart_set_all_value(lv_obj_t * obj, lv_chart_series_t * ser, lv_coord_t value);

/**
 * Set the next point's Y value according to the update mode policy.
 * @param obj       pointer to chart object
 * @param ser       pointer to a data series on 'chart'
 * @param value     the new value of the next data
 */
void hpi_lv_chart_set_next_value(lv_obj_t * obj, lv_chart_series_t * ser, lv_coord_t value);

/**
 * Set the next point's X and Y value according to the update mode policy.
 * @param obj       pointer to chart object
 * @param ser       pointer to a data series on 'chart'
 * @param x_value   the new X value of the next data
 * @param y_value   the new Y value of the next data
 */
void hpi_lv_chart_set_next_value2(lv_obj_t * obj, lv_chart_series_t * ser, lv_coord_t x_value, lv_coord_t y_value);

/**
 * Set an individual point's y value of a chart's series directly based on its index
 * @param obj     pointer to a chart object
 * @param ser     pointer to a data series on 'chart'
 * @param id      the index of the x point in the array
 * @param value   value to assign to array point
 */
void hpi_lv_chart_set_value_by_id(lv_obj_t * obj, lv_chart_series_t * ser, uint16_t id, lv_coord_t value);

/**
 * Set an individual point's x and y value of a chart's series directly based on its index
 * Can be used only with `LV_CHART_TYPE_SCATTER`.
 * @param obj       pointer to chart object
 * @param ser       pointer to a data series on 'chart'
 * @param id        the index of the x point in the array
 * @param x_value   the new X value of the next data
 * @param y_value   the new Y value of the next data
 */
void hpi_lv_chart_set_value_by_id2(lv_obj_t * obj, lv_chart_series_t * ser, uint16_t id, lv_coord_t x_value,
                               lv_coord_t y_value);

/**
 * Set an external array for the y data points to use for the chart
 * NOTE: It is the users responsibility to make sure the `point_cnt` matches the external array size.
 * @param obj       pointer to a chart object
 * @param ser       pointer to a data series on 'chart'
 * @param array     external array of points for chart
 */
void hpi_lv_chart_set_ext_y_array(lv_obj_t * obj, lv_chart_series_t * ser, lv_coord_t array[]);

/**
 * Set an external array for the x data points to use for the chart
 * NOTE: It is the users responsibility to make sure the `point_cnt` matches the external array size.
 * @param obj       pointer to a chart object
 * @param ser       pointer to a data series on 'chart'
 * @param array     external array of points for chart
 */
void hpi_lv_chart_set_ext_x_array(lv_obj_t * obj, lv_chart_series_t * ser, lv_coord_t array[]);

/**
 * Get the array of y values of a series
 * @param obj   pointer to a chart object
 * @param ser   pointer to a data series on 'chart'
 * @return      the array of values with 'point_count' elements
 */
lv_coord_t * hpi_lv_chart_get_y_array(const lv_obj_t * obj, lv_chart_series_t * ser);

/**
 * Get the array of x values of a series
 * @param obj   pointer to a chart object
 * @param ser   pointer to a data series on 'chart'
 * @return      the array of values with 'point_count' elements
 */
lv_coord_t * hpi_lv_chart_get_x_array(const lv_obj_t * obj, lv_chart_series_t * ser);

/**
 * Get the index of the currently pressed point. It's the same for every series.
 * @param obj       pointer to a chart object
 * @return          the index of the point [0 .. point count] or hpi_lv_chart_POINT_ID_NONE if no point is being pressed
 */
uint32_t lv_chart_get_pressed_point(const lv_obj_t * obj);

/**********************
 *      MACROS
 **********************/

#endif /*LV_USE_CHART*/

#ifdef __cplusplus
} /*extern "C"*/
#endif

//#endif /*LV_CHART_H*/
