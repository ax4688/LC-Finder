﻿/* ***************************************************************************
 * view_picture.c -- picture view
 *
 * Copyright (C) 2016 by Liu Chao <lc-soft@live.cn>
 *
 * This file is part of the LC-Finder project, and may only be used, modified,
 * and distributed under the terms of the GPLv2.
 *
 * By continuing to use, modify, or distribute this file you indicate that you
 * have read the license and understand and accept it fully.
 *
 * The LC-Finder project is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GPL v2 for more details.
 *
 * You should have received a copy of the GPLv2 along with this file. It is
 * usually in the LICENSE.TXT file, If not, see <http://www.gnu.org/licenses/>.
 * ****************************************************************************/

/* ****************************************************************************
 * view_picture.c -- "图片" 视图，用于显示单张图片，并提供缩放、切换等功能
 *
 * 版权所有 (C) 2016 归属于 刘超 <lc-soft@live.cn>
 *
 * 这个文件是 LC-Finder 项目的一部分，并且只可以根据GPLv2许可协议来使用、更改和
 * 发布。
 *
 * 继续使用、修改或发布本文件，表明您已经阅读并完全理解和接受这个许可协议。
 *
 * LC-Finder 项目是基于使用目的而加以散布的，但不负任何担保责任，甚至没有适销
 * 性或特定用途的隐含担保，详情请参照GPLv2许可协议。
 *
 * 您应已收到附随于本文件的GPLv2许可协议的副本，它通常在 LICENSE 文件中，如果
 * 没有，请查看：<http://www.gnu.org/licenses/>.
 * ****************************************************************************/

#include <math.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include "finder.h"
#include "ui.h"
#include <LCUI/timer.h>
#include <LCUI/display.h>
#include <LCUI/cursor.h>
#include <LCUI/graph.h>
#include <LCUI/gui/builder.h>
#include <LCUI/gui/widget.h>
#include <LCUI/gui/widget/textview.h>
#include "dialog.h"

#define MAX_SCALE	5.0
#define XML_PATH	"res/ui-view-picture.xml"
#define TEXT_DELETE	L"删除此文件？"

#define BindEvent(W, E, CB) Widget_BindEvent( W, E, CB, NULL, NULL )

/** 图片实例 */
typedef struct PcitureRec_ {
	wchar_t *file;			/**< 当前已加载的图片的路径  */
	wchar_t *file_for_load;		/**< 当前需加载的图片的路径  */
	LCUI_BOOL is_loading;		/**< 是否正在载入图片 */
	LCUI_Graph data;		/**< 当前已经加载的图片数据 */
	LCUI_Widget view;		/**< 视图，用于呈现该图片 */
	int timer;			/**< 定时器，用于延迟显示“载入中...”提示框 */
} PictureRec, *Picture;

/** 图片查看器相关数据 */
static struct PictureViewer {
	LCUI_Widget window;			/**< 图片查看器窗口 */
	LCUI_Widget tip;			/**< 图片载入提示框，当图片正在载入时显示 */
	LCUI_Widget tip_empty;			/**< 提示，当无内容时显示 */
	LCUI_Widget btn_reset;			/**< 按钮，重置图片尺寸 */
	LCUI_Widget btn_prev;			/**< “上一张”按钮 */
	LCUI_Widget btn_next;			/**< “下一张”按钮 */
	LCUI_Widget view_pictures;		/**< 视图，用于容纳多个图片视图 */
	LCUI_BOOL is_working;			/**< 当前视图是否在工作 */
	LCUI_BOOL is_opening;			/**< 当前视图是否为打开状态 */
	LCUI_BOOL is_zoom_mode;			/**< 当前视图是否为放大/缩小模式 */
	PictureRec pictures[3];			/**< 三组图片实例 */
	int picture_i;				/**< 当前需加载的图片的序号（下标） */
	Picture picture;			/**< 当前焦点图片 */
	double scale;				/**< 图片缩放比例 */
	double min_scale;			/**< 图片最小缩放比例 */
	LCUI_Cond cond;				/**< 条件变量 */
	LCUI_Mutex mutex;			/**< 互斥锁 */
	LCUI_Thread th_picloader;		/**< 图片加载器线程 */
	FileIterator iterator;			/**< 图片文件列表迭代器 */
	int focus_x, focus_y;			/**< 当前焦点位置，相对于按比例缩放后的图片 */
	int origin_focus_x, origin_focus_y;	/**< 当前焦点位置，相对于原始尺寸的图片 */
	int offset_x, offset_y;			/**< 浏览区域的位置偏移量，相对于焦点位置 */
	struct PictureDraging {			/**< 图片拖拽功能的相关数据 */
		int focus_x, focus_y;		/**< 拖动开始时的图片坐标，相对于按比例缩放后的图片 */
		int mouse_x, mouse_y;		/**< 拖动开始时的鼠标坐标 */
		LCUI_BOOL is_running;		/**< 是否正在执行拖动操作 */
		LCUI_BOOL is_started;		/**< 是否已经拖动过一次 */
		LCUI_BOOL with_x;		/**< 拖动时是否需要改变图片X坐标 */
		LCUI_BOOL with_y;		/**< 拖动时是否需要改变图片Y坐标 */
	} drag;					/**< 实现图片拖动浏览功能所需的数据 */
	struct PictureTouchZoom {		/**< 图片触控缩放功能的相关数据 */
		int point_ids[2];		/**< 两个触点的ID */
		int distance;			/**< 触点距离 */
		double scale;			/**< 缩放开始前的缩放比例 */
		LCUI_BOOL is_running;		/**< 缩放是否正在进行 */
		int x, y;			/**< 缩放开始前的触点位置 */
	} zoom;
} this_view;

/** 更新图片切换按钮的状态 */
static void UpdateSwitchButtons( void )
{
	FileIterator iter = this_view.iterator;
	Widget_Hide( this_view.btn_prev );
	Widget_Hide( this_view.btn_next );
	if( iter ) {
		if( iter->index > 0 ) {
			Widget_Show( this_view.btn_prev );
		}
		if( iter->length >= 1 && iter->index < iter->length - 1 ) {
			Widget_Show( this_view.btn_next );
		}
	}
}

static int OpenPrevPicture( void )
{
	PictureRec pic;
	FileIterator iter = this_view.iterator;
	if( !iter || iter->index == 0 ) {
		return -1;
	}
	iter->prev( iter );
	UpdateSwitchButtons();
	pic = this_view.pictures[2];
	Widget_Prepend( this_view.view_pictures, pic.view );
	this_view.pictures[2] = this_view.pictures[1];
	this_view.pictures[1] = this_view.pictures[0];
	this_view.pictures[0] = pic;
	UI_OpenPictureView( iter->filepath );
	return 0;
}

static int OpenNextPicture( void )
{
	PictureRec pic;
	FileIterator iter = this_view.iterator;
	if( !iter || iter->index >= iter->length - 1 ) {
		return -1;
	}
	iter->next( iter );
	UpdateSwitchButtons();
	pic = this_view.pictures[0];
	Widget_Append( this_view.view_pictures, pic.view );
	this_view.pictures[0] = this_view.pictures[1];
	this_view.pictures[1] = this_view.pictures[2];
	this_view.pictures[2] = pic;
	UI_OpenPictureView( iter->filepath );
	return 0;
}

static void UpdateResetSizeButton( void )
{
	LCUI_Widget txt = LinkedList_Get( &this_view.btn_reset->children, 0 );
	if( this_view.scale == 1.0 ) {
		Widget_RemoveClass( txt, "mdi-fullscreen" );
		Widget_AddClass( txt, "mdi-fullscreen-exit" );
	} else {
		Widget_RemoveClass( txt, "mdi-fullscreen-exit" );
		Widget_AddClass( txt, "mdi-fullscreen" );
	}
}

/** 更新图片位置 */
static void UpdatePicturePosition( void )
{
	LCUI_Style sheet;
	int x, y, width, height;
	Picture pic = this_view.picture;
	sheet = pic->view->custom_style->sheet;
	width = (int)(pic->data.width * this_view.scale);
	height = (int)(pic->data.height * this_view.scale);
	/* 若缩放后的图片宽度小于图片查看器的宽度 */
	if( width <= pic->view->width ) {
		/* 设置拖动时不需要改变X坐标，且图片水平居中显示 */
		this_view.drag.with_x = FALSE;
		this_view.focus_x = width / 2;
		this_view.origin_focus_x = pic->data.width / 2;
		x = (pic->view->width - width) / 2;
		SetStyle( pic->view->custom_style,
			  key_background_position_x, x, px );
	} else {
		this_view.drag.with_x = TRUE;
		x = this_view.origin_focus_x;
		this_view.focus_x = (int)(x * this_view.scale + 0.5);
		x = this_view.focus_x - this_view.offset_x;
		/* X坐标调整，确保查看器的图片浏览范围不超出图片 */
		if( x < 0 ) {
			x = 0;
			this_view.focus_x = this_view.offset_x;
		}
		if( x + pic->view->width > width ) {
			x = width - pic->view->width;
			this_view.focus_x = x + this_view.offset_x;
		}
		SetStyle( pic->view->custom_style,
			  key_background_position_x, -x, px );
		/* 根据缩放后的焦点坐标，计算出相对于原始尺寸图片的焦点坐标 */
		x = (int)(this_view.focus_x / this_view.scale + 0.5);
		this_view.origin_focus_x = x;
	}
	/* 原理同上 */
	if( height <= pic->view->height ) {
		this_view.drag.with_y = FALSE;
		this_view.focus_y = height / 2;
		this_view.origin_focus_y = pic->data.height / 2;
		sheet[key_background_position_y].is_valid = FALSE;
		y = (pic->view->height - height) / 2;
		SetStyle( pic->view->custom_style,
			  key_background_position_y, y, px );
	} else {
		this_view.drag.with_y = TRUE;
		y = this_view.origin_focus_y;
		this_view.focus_y = (int)(y * this_view.scale + 0.5);
		y = this_view.focus_y - this_view.offset_y;
		if( y < 0 ) {
			y = 0;
			this_view.focus_y = this_view.offset_y;
		}
		if( y + pic->view->height > height ) {
			y = height - pic->view->height;
			this_view.focus_y = y + this_view.offset_y;
		}
		SetStyle( pic->view->custom_style,
			  key_background_position_y, -y, px );
		y = (int)(this_view.focus_y / this_view.scale + 0.5);
		this_view.origin_focus_y = y;
	}
	Widget_UpdateStyle( pic->view, FALSE );
}

/** 重置浏览区域的位置偏移量 */
static void ResetOffsetPosition( void )
{
	this_view.offset_x = this_view.picture->view->width / 2;
	this_view.offset_y = this_view.picture->view->height / 2;
}

/** 重置当前显示的图片的尺寸 */
static void ResetPictureSize( void )
{
	LCUI_Style sheet;
	double scale_x, scale_y;
	Picture pic = this_view.picture;
	if( !Graph_IsValid( &pic->data ) ) {
		return;
	}
	/* 如果尺寸小于图片查看器尺寸 */
	if( pic->data.width < pic->view->width && 
	    pic->data.height < pic->view->height ) {
		Widget_RemoveClass( pic->view, "contain-mode" );
		this_view.scale = 1.0;
	} else {
		Widget_AddClass( pic->view, "contain-mode" );
		scale_x = 1.0 * pic->view->width / pic->data.width;
		scale_y = 1.0 * pic->view->height / pic->data.height;
		if( scale_y < scale_x ) {
			this_view.scale = scale_y;
		} else {
			this_view.scale = scale_x;
		}
		if( this_view.scale < 0.05 ) {
			this_view.scale = 0.05;
		}
		this_view.min_scale = this_view.scale;
	}
	sheet = pic->view->custom_style->sheet;
	sheet[key_background_size].is_valid = FALSE;
	sheet[key_background_size_width].is_valid = FALSE;
	sheet[key_background_size_height].is_valid = FALSE;
	Widget_UpdateStyle( pic->view, FALSE );
	ResetOffsetPosition();
	UpdatePicturePosition();
	UpdateResetSizeButton();
}

/** 在返回按钮被点击的时候 */
static OnBtnBackClick( LCUI_Widget w, LCUI_WidgetEvent e, void *arg )
{
	UI_ClosePictureView();
}

/** 在图片视图尺寸发生变化的时候 */
static void OnPictureResize( LCUI_Widget w, LCUI_WidgetEvent e, void *arg )
{
	if( this_view.is_zoom_mode ) {
		UpdatePicturePosition();
	} else {
		ResetPictureSize();
	}
}

static void OnShowTipLoading( void *arg )
{
	Picture pic = arg;
	if( this_view.picture == pic ) {
		if( pic->is_loading ) {
			Widget_Show( this_view.tip );
		}
	}
	pic->timer = 0;
}

/** 设置当前图片缩放比例 */
static void SetPictureScale( double scale )
{
	int width, height;
	LCUI_StyleSheet sheet;
	if( scale < this_view.min_scale ) {
		scale = this_view.min_scale;
		this_view.is_zoom_mode = FALSE;
	} else {
		this_view.is_zoom_mode = TRUE;
	}
	if( scale > MAX_SCALE ) {
		scale = MAX_SCALE;
	}
	sheet = this_view.picture->view->custom_style;
	width = (int)(scale * this_view.picture->data.width);
	height = (int)(scale * this_view.picture->data.height);
	SetStyle( sheet, key_background_size_width, width, px );
	SetStyle( sheet, key_background_size_height, height, px );
	Widget_RemoveClass( this_view.picture->view, "contain-mode" );
	Widget_UpdateStyle( this_view.picture->view, FALSE );
	this_view.scale = scale;
	UpdatePicturePosition();
	UpdateResetSizeButton();
}

static void DragPicture( int mouse_x, int mouse_y )
{
	LCUI_StyleSheet sheet;
	Picture pic = this_view.picture;
	if( !this_view.drag.is_running ) {
		return;
	}
	sheet = pic->view->custom_style;
	if( this_view.drag.with_x ) {
		int x, width;
		width = (int)(pic->data.width * this_view.scale);
		this_view.focus_x = this_view.drag.focus_x;
		this_view.focus_x -= mouse_x - this_view.drag.mouse_x;
		x = this_view.focus_x - this_view.offset_x;
		if( x < 0 ) {
			x = 0;
			this_view.focus_x = this_view.offset_x;
		}
		if( x + pic->view->width > width ) {
			x = width - pic->view->width;
			this_view.focus_x = x + this_view.offset_x;
		}
		SetStyle( pic->view->custom_style,
			  key_background_position_x, -x, px );
		x = (int)(this_view.focus_x / this_view.scale);
		this_view.origin_focus_x = x;
	} else {
		SetStyle( sheet, key_background_position_x, 0.5, scale );
	}
	if( this_view.drag.with_y ) {
		int y, height;
		height = (int)(pic->data.height * this_view.scale);
		this_view.focus_y = this_view.drag.focus_y;
		this_view.focus_y -= mouse_y - this_view.drag.mouse_y;
		y = this_view.focus_y - this_view.offset_y;
		if( y < 0 ) {
			y = 0;
			this_view.focus_y = this_view.offset_y;
		}
		if( y + pic->view->height > height ) {
			y = height - pic->view->height;
			this_view.focus_y = y + this_view.offset_y;
		}
		SetStyle( pic->view->custom_style,
			  key_background_position_y, -y, px );
		y = (int)(this_view.focus_y / this_view.scale);
		this_view.origin_focus_y = y;
	} else {
		SetStyle( sheet, key_background_position_y, 0.5, scale );
	}
	Widget_UpdateStyle( pic->view, FALSE );
}

/** 当鼠标在图片容器上移动的时候 */
static void OnPictureMouseMove( LCUI_Widget w, LCUI_WidgetEvent e, void *arg )
{
	DragPicture( e->motion.x, e->motion.y );
}

/** 当鼠标按钮在图片容器上释放的时候 */
static void OnPictureMouseUp( LCUI_Widget w, LCUI_WidgetEvent e, void *arg )
{
	this_view.drag.is_running = FALSE;
	Widget_UnbindEvent( w, "mouseup", OnPictureMouseUp );
	Widget_UnbindEvent( w, "mousemove", OnPictureMouseMove );
	Widget_ReleaseMouseCapture( w );
}

/** 当鼠标按钮在图片容器上点住的时候 */
static void OnPictureMouseDown( LCUI_Widget w, LCUI_WidgetEvent e, void *arg )
{
	this_view.drag.is_started = TRUE;
	this_view.drag.is_running = TRUE;
	this_view.drag.mouse_x = e->motion.x;
	this_view.drag.mouse_y = e->motion.y;
	this_view.drag.focus_x = this_view.focus_x;
	this_view.drag.focus_y = this_view.focus_y;
	Widget_BindEvent( w, "mousemove", OnPictureMouseMove, NULL, NULL );
	Widget_BindEvent( w, "mouseup",  OnPictureMouseUp, NULL, NULL );
	Widget_SetMouseCapture( w );
}

static void OnPictureTouchDown( LCUI_TouchPoint point )
{
	this_view.drag.is_started = TRUE;
	this_view.drag.is_running = TRUE;
	this_view.drag.mouse_x = point->x;
	this_view.drag.mouse_y = point->y;
	this_view.drag.focus_x = this_view.focus_x;
	this_view.drag.focus_y = this_view.focus_y;
	Widget_SetTouchCapture( this_view.picture->view, point->id );
}

static void OnPictureTouchUp( LCUI_TouchPoint point )
{
	this_view.drag.is_running = FALSE;
	Widget_ReleaseTouchCapture( this_view.picture->view, -1 );
}

static void OnPictureTouchMove( LCUI_TouchPoint point )
{
	DragPicture( point->x, point->y );
}

static void OnPictureTouch( LCUI_Widget w, LCUI_WidgetEvent e, void *arg )
{
	double distance, scale;
	int i, x, y, *point_ids;
	LCUI_TouchPoint points[2] = {NULL, NULL};
	if( e->touch.n_points == 0 ) {
		return;
	}
	point_ids = this_view.zoom.point_ids;
	if( e->touch.n_points == 1 ) {
		LCUI_TouchPoint point;
		point = &e->touch.points[0];
		switch( point->state ) {
		case WET_TOUCHDOWN: 
			OnPictureTouchDown( point );
			point_ids[0] = point->id;
			break;
		case WET_TOUCHMOVE:
			OnPictureTouchMove( point );
			break;
		case WET_TOUCHUP: 
			OnPictureTouchUp( point ); 
			point_ids[0] = -1;
			break;
		default: break;
		}
		return;
	}
	this_view.drag.is_running = FALSE;
	/* 遍历各个触点，确定两个用于缩放功能的触点 */
	for( i = 0; i < e->touch.n_points; ++i ) {
		if( point_ids[0] == -1 ) {
			points[0] = &e->touch.points[i];
			point_ids[0] = points[0]->id;
			continue;
		} else if( point_ids[0] == e->touch.points[i].id ) {
			points[0] = &e->touch.points[i];
			continue;
		}
		if( this_view.zoom.point_ids[1] == -1 ) {
			points[1] = &e->touch.points[i];
			point_ids[1] = points[1]->id;
		} else if( point_ids[1] == e->touch.points[i].id ) {
			points[1] = &e->touch.points[i];
		}
	}
	/* 计算两触点的距离 */
	distance = pow( points[0]->x - points[1]->x, 2 );
	distance += pow( points[0]->y - points[1]->y, 2 );
	distance = sqrt( distance );
	if( !this_view.zoom.is_running ) {
		x = points[0]->x - (points[0]->x - points[1]->x) / 2;
		y = points[0]->y - (points[0]->y - points[1]->y) / 2;
		this_view.zoom.distance = (int)distance;
		this_view.zoom.scale = this_view.scale;
		this_view.zoom.is_running = TRUE;
		this_view.zoom.x = x;
		this_view.zoom.y = y;
	} else if( points[0]->state == WET_TOUCHMOVE ||
		   points[1]->state == WET_TOUCHMOVE ) {
		/* 重新计算焦点位置 */
		x = this_view.focus_x - this_view.offset_x;
		y = this_view.focus_y - this_view.offset_y;
		this_view.offset_x = this_view.zoom.x;
		this_view.offset_y = this_view.zoom.y;
		this_view.focus_x = x + this_view.offset_x;
		this_view.focus_y = y + this_view.offset_y;
		x = (int)(this_view.focus_x / this_view.scale + 0.5);
		y = (int)(this_view.focus_y / this_view.scale + 0.5);
		this_view.origin_focus_x = x;
		this_view.origin_focus_y = y;
		scale = this_view.zoom.scale;
		scale *= distance / this_view.zoom.distance;
		SetPictureScale( scale );
	}
	for( i = 0; i < 2; ++i ) {
		int j, id;
		/* 如果当前用于控制缩放的触点已经释放，则找其它触点来代替 */
		if( points[i]->state != WET_TOUCHUP ) {
			continue;
		}
		point_ids[i] = -1;
		this_view.zoom.is_running = FALSE;
		id = point_ids[i == 0 ? 1 : 0];
		for( j = 0; j < e->touch.n_points; ++j ) {
			if( e->touch.points[j].state == WET_TOUCHUP ||
			    e->touch.points[j].id == id ) {
				continue;
			}
			points[i] = &e->touch.points[j];
			point_ids[i] = points[i]->id;
			i -= 1;
			break;
		}
	}
	if( point_ids[0] == -1 ) {
		point_ids[0] = point_ids[1];
		point_ids[1] = -1;
		this_view.zoom.is_running = FALSE;
	} else if( point_ids[1] == -1 ) {
		this_view.zoom.is_running = FALSE;
	}
}

/** 初始化图片实例 */
static void InitPicture( Picture pic )
{
	pic->timer = 0;
	pic->file = NULL;
	pic->is_loading = FALSE;
	pic->file_for_load = NULL;
	pic->view = LCUIWidget_New( "picture" );
	Graph_Init( &pic->data );
	Widget_Append( this_view.view_pictures, pic->view );
	BindEvent( pic->view, "resize", OnPictureResize );
	BindEvent( pic->view, "touch", OnPictureTouch );
	BindEvent( pic->view, "mousedown", OnPictureMouseDown );
}

/** 设置图片 */
static void SetPicture( Picture pic, const wchar_t *file )
{
	int len;
	if( pic->timer > 0 ) {
		LCUITimer_Free( pic->timer );
		pic->timer = 0;
	}
	if( pic->file_for_load ) {
		free( pic->file_for_load );
		pic->file_for_load = NULL;
	}
	if( file ) {
		len = wcslen( file ) + 1;
		pic->file_for_load = malloc( sizeof( wchar_t ) * len );
		wcsncpy( pic->file_for_load, file, len );
	} else {
		pic->file_for_load = NULL;
	}
}

/** 设置图片预加载列表 */
static void SetPicturePreloadList( void )
{
	wchar_t *wpath;
	FileIterator iter;
	if( !this_view.iterator ) {
		return;
	}
	iter = this_view.iterator;
	/* 预加载上一张图片 */
	if( iter->index > 0 ) {
		iter->prev( iter );
		if( iter->filepath ) {
			wpath = DecodeUTF8( iter->filepath );
			SetPicture( &this_view.pictures[0], wpath );
		}
		iter->next( iter );
	}
	/* 预加载下一张图片 */
	if( iter->index < iter->length - 1 ) {
		iter->next( iter );
		if( iter->filepath ) {
			wpath = DecodeUTF8( iter->filepath );
			SetPicture( &this_view.pictures[2], wpath );
		}
		iter->prev( iter );
	}
}

/** 加载图片数据 */
static int LoadPicture( Picture pic )
{
	int len;
	char *path;
	wchar_t *wpath;
#ifdef _WIN32
	int encoding = ENCODING_ANSI;
#else
	int encoding = ENCODING_UTF8;
#endif
	LCUI_StyleSheet sheet = pic->view->custom_style;
	if( !pic->file_for_load || !this_view.is_working ) {
		return -1;
	}
	/* 避免重复加载 */
	if( pic->file && wcscmp( pic->file, pic->file_for_load ) == 0 ) {
		free( pic->file_for_load );
		pic->file_for_load = NULL;
		return 0;
	}
	/** 500毫秒后显示 "图片载入中..." 的提示 */
	this_view.picture->timer = LCUITimer_Set( 500, OnShowTipLoading, 
						  this_view.picture, FALSE );
	if( Graph_IsValid( &pic->data ) ) {
		Widget_Lock( pic->view );
		Graph_Free( &pic->data );
		sheet->sheet[key_background_image].is_valid = FALSE;
		Widget_UpdateBackground( pic->view );
		Widget_UpdateStyle( pic->view, FALSE );
		Widget_Unlock( pic->view );
	}
	wpath = pic->file_for_load;
	pic->file_for_load = NULL;
	if( pic->file ) {
		free( pic->file );
	}
	pic->file = wpath;
	pic->is_loading = TRUE;
	len = LCUI_EncodeString( NULL, wpath, 0, encoding ) + 1;
	path = malloc( sizeof( char ) * len );
	LCUI_EncodeString( path, wpath, len, encoding );
	Graph_LoadImage( path, &pic->data );
	/* 如果在加载完后没有待加载的图片，则直接呈现到部件上 */
	if( !pic->file_for_load ) {
		SetStyle( sheet, key_background_image, &pic->data, image );
		Widget_UpdateStyle( pic->view, FALSE );
		ResetPictureSize();
	}
	pic->is_loading = FALSE;
	if( this_view.picture->view == pic->view ) {
		Widget_Hide( this_view.tip );
	}
	free( path );
	return 0;
}

/** 在”放大“按钮被点击的时候 */
static void OnBtnZoomInClick( LCUI_Widget w, LCUI_WidgetEvent e, void *arg )
{
	ResetOffsetPosition();
	SetPictureScale( this_view.scale + 0.5 );
}

/** 在”缩小“按钮被点击的时候 */
static void OnBtnZoomOutClick( LCUI_Widget w, LCUI_WidgetEvent e, void *arg )
{
	ResetOffsetPosition();
	SetPictureScale( this_view.scale - 0.5 );
}

static void OnBtnPrevClick( LCUI_Widget w, LCUI_WidgetEvent e, void *arg )
{
	OpenPrevPicture();
}
static void OnBtnNextClick( LCUI_Widget w, LCUI_WidgetEvent e, void *arg )
{
	OpenNextPicture();
}
static void OnBtnDeleteClick( LCUI_Widget w, LCUI_WidgetEvent e, void *arg )
{
	int len;
	char *path;
	wchar_t *wpath = this_view.picture->file;
	if( !LCUIDialog_Confirm( this_view.window, TEXT_DELETE, NULL ) ) {
		return;
	}
	len = LCUI_EncodeString( NULL, wpath, 0, ENCODING_UTF8 ) + 1;
	path = malloc( sizeof( char ) * len );
	LCUI_EncodeString( path, wpath, len, ENCODING_UTF8 );
	if( OpenNextPicture() != 0 ) {
		/** 如果前后都没有图片了，则提示没有可显示的内容 */
		if( OpenPrevPicture() != 0 ) {
			Widget_RemoveClass( this_view.tip_empty, "hide" );
			Widget_Show( this_view.tip_empty );
		}
	}
	LCFinder_DeleteFiles( &path, 1, NULL, NULL );
	LCFinder_TriggerEvent( EVENT_FILE_DEL, path );
	free( path );
}

/** 在”实际大小“按钮被点击的时候 */
static void OnBtnResetClick( LCUI_Widget w, LCUI_WidgetEvent e, void *arg )
{
	double scale;
	if( this_view.scale == 1.0 ) {
		scale = this_view.min_scale;
	} else {
		scale = 1.0;
	}
	ResetOffsetPosition();
	SetPictureScale( scale );
}

/** 图片加载器 */
static void PictureLoader( void *arg )
{
	LCUIMutex_Lock( &this_view.mutex );
	while( this_view.is_working ) {
		LCUICond_Wait( &this_view.cond, &this_view.mutex );
		this_view.picture_i = 0;
		while( this_view.picture_i < 3 ) {
			int i;
			switch( this_view.picture_i ) {
			case 0: i = 1; break;
			case 1: i = 2; break;
			case 2: i = 0; break;
			default: break;
			}
			LoadPicture( &this_view.pictures[i] );
			++this_view.picture_i;
		}
	}
	LCUIMutex_Unlock( &this_view.mutex );
	LCUIThread_Exit( NULL );
}

static void OnBtnShowInfoClick( LCUI_Widget w, LCUI_WidgetEvent e, void *arg )
{
	UI_ShowPictureInfoView();
}

void UI_InitPictureView( void )
{
	LCUI_Widget box, btn_back, btn_info, btn[4];
	box = LCUIBuilder_LoadFile( XML_PATH );
	if( box ) {
		Widget_Top( box );
		Widget_Unwrap( box );
	}
	this_view.is_working = TRUE;
	this_view.is_opening = FALSE;
	this_view.zoom.is_running = FALSE;
	this_view.zoom.point_ids[0] = -1;
	this_view.zoom.point_ids[1] = -1;
	LCUICond_Init( &this_view.cond );
	LCUIMutex_Init( &this_view.mutex );
	btn_info = LCUIWidget_GetById( ID_BTN_SHOW_PICTURE_INFO );
	btn_back = LCUIWidget_GetById( ID_BTN_HIDE_PICTURE_VIEWER );
	this_view.btn_prev = LCUIWidget_GetById( ID_BTN_PCITURE_PREV );
	this_view.btn_next = LCUIWidget_GetById( ID_BTN_PCITURE_NEXT );
	btn[0] = LCUIWidget_GetById( ID_BTN_PICTURE_RESET_SIZE );
	btn[1] = LCUIWidget_GetById( ID_BTN_PICTURE_ZOOM_IN );
	btn[2] = LCUIWidget_GetById( ID_BTN_PICTURE_ZOOM_OUT );
	btn[3] = LCUIWidget_GetById( ID_BTN_DELETE_PICTURE );
	this_view.tip = LCUIWidget_GetById( ID_TIP_PICTURE_LOADING );
	this_view.window = LCUIWidget_GetById( ID_WINDOW_PCITURE_VIEWER );
	this_view.tip_empty = LCUIWidget_GetById( ID_TIP_PICTURE_NOT_FOUND );
	this_view.view_pictures = LCUIWidget_GetById( ID_VIEW_PICTURE_TARGET );
	this_view.btn_reset = btn[0];
	InitPicture( &this_view.pictures[0] );
	InitPicture( &this_view.pictures[1] );
	InitPicture( &this_view.pictures[2] );
	this_view.picture = &this_view.pictures[0];
	BindEvent( btn_info, "click", OnBtnShowInfoClick );
	BindEvent( btn_back, "click", OnBtnBackClick );
	BindEvent( btn[0], "click", OnBtnResetClick );
	BindEvent( btn[1], "click", OnBtnZoomInClick );
	BindEvent( btn[2], "click", OnBtnZoomOutClick );
	BindEvent( btn[3], "click", OnBtnDeleteClick );
	BindEvent( this_view.btn_prev, "click", OnBtnPrevClick );
	BindEvent( this_view.btn_next, "click", OnBtnNextClick );
	LCUIThread_Create( &this_view.th_picloader, PictureLoader, NULL );
	Widget_Hide( this_view.window );
	UI_InitPictureInfoView();
	UpdateSwitchButtons();
}

void UI_OpenPictureView( const char *filepath )
{
	int len;
	wchar_t *wpath;
	LCUI_Widget main_window, root;

	_DEBUG_MSG("open: %s\n", filepath);
	len = strlen( filepath );
	root = LCUIWidget_GetRoot();
	LCUIMutex_Lock( &this_view.mutex );
	wpath = calloc( len + 1, sizeof( wchar_t ) );
	this_view.picture_i = -1;
	this_view.is_opening = TRUE;
	this_view.is_zoom_mode = FALSE;
	this_view.picture = &this_view.pictures[1];
	LCUI_DecodeString( wpath, filepath, len, ENCODING_UTF8 );
	main_window = LCUIWidget_GetById( ID_WINDOW_MAIN );
	Widget_SetTitleW( root, wgetfilename( wpath ) );
	SetPicture( this_view.picture, wpath );
	SetPicturePreloadList();
	LCUIMutex_Unlock( &this_view.mutex );
	LCUICond_Signal( &this_view.cond );
	UI_SetPictureInfoView( filepath );
	Widget_Hide( this_view.tip_empty );
	Widget_Show( this_view.window );
	Widget_Hide( main_window );
	free( wpath );
}

void UI_SetPictureView( FileIterator iter )
{
	if( this_view.iterator ) {
		this_view.iterator->destroy( this_view.iterator );
		this_view.iterator = NULL;
	}
	if( iter ) {
		this_view.iterator = iter;
	}
	UpdateSwitchButtons();
}

void UI_ClosePictureView( void )
{
	LCUI_Widget root = LCUIWidget_GetRoot();
	LCUI_Widget main_window = LCUIWidget_GetById( ID_WINDOW_MAIN );
	Widget_SetTitleW( root, L"LC-Finder" );
	ResetPictureSize();
	Widget_Show( main_window );
	Widget_Hide( this_view.window );
	this_view.is_opening = FALSE;
	LCUICond_Signal( &this_view.cond );
	UI_SetPictureView( NULL );
}

void UI_ExitPictureView( void )
{
	this_view.is_working = FALSE;
	LCUIMutex_Lock( &this_view.mutex );
	LCUICond_Signal( &this_view.cond );
	LCUIMutex_Unlock( &this_view.mutex );
	LCUIThread_Join( this_view.th_picloader, NULL );
	LCUIMutex_Destroy( &this_view.mutex );
	LCUICond_Destroy( &this_view.cond );
}