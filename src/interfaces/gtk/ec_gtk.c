/*
    ettercap -- GTK+ GUI

    Copyright (C) ALoR & NaGA

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

    $Id: ec_gtk.c,v 1.12 2004/03/03 13:52:35 daten Exp $
*/

#include <ec.h>

#include <ec_gtk.h>
#include <ec_capture.h>
#include <ec_version.h>

#include <pcap.h>

/* globals */

GtkWidget *window = NULL;   /* main window */
GtkWidget *notebook = NULL;
GtkWidget *main_menu = NULL;

static GtkWidget     *textview = NULL;
static GtkTextBuffer *msgbuffer = NULL;
static GtkTextMark   *endmark = NULL;
static GtkAccelGroup *accel_group = NULL;

/* proto */

void set_gtk_interface(void);
void gtkui_start(void);

void gtkui_message(const char *msg);
void gtkui_input(const char *title, char *input, size_t n, void (*callback)(void));
   
static void gtkui_init(void);
static void gtkui_cleanup(void);
static void gtkui_msg(const char *msg);
static void gtkui_error(const char *msg);
static void gtkui_fatal_error(const char *msg);
static gboolean gtkui_flush_msg(gpointer data);
static void gtkui_progress(char *title, int value, int max);

static void gtkui_setup(void);
static void gtkui_exit(void);

static void toggle_unoffensive(void);
static void toggle_nopromisc(void);

static void gtkui_file_open(void);
static void read_pcapfile(char *file);
static void gtkui_file_write(void);
static void write_pcapfile(void);
static void gtkui_unified_sniff(void);
static void gtkui_unified_sniff_default(void);
static void gtkui_bridged_sniff(void);
static void bridged_sniff(void);
static void gtkui_pcap_filter(void);

GtkTextBuffer *gtkui_details_window(char *title);
void gtkui_details_print(GtkTextBuffer *textbuf, char *data);
void gtkui_dialog_enter(GtkWidget *widget, gpointer data);
gboolean gtkui_context_menu(GtkWidget *widget, GdkEventButton *event, gpointer data);

/* MDI pages */
GtkWidget *gtkui_page_new(char *title, void (*callback)(void), void (*detacher)(GtkWidget *));
void gtkui_page_present(GtkWidget *child);
void gtkui_page_close(GtkWidget *widget, gpointer data);
void gtkui_page_close_current(void);
void gtkui_page_detach_current(void);
void gtkui_page_right(void);
void gtkui_page_left(void);
static void gtkui_page_defocus_tabs(void);

/***#****************************************/

void set_gtk_interface(void)
{
   struct ui_ops ops;

   /* wipe the struct */
   memset(&ops, 0, sizeof(ops));

   /* register the functions */
   ops.init = &gtkui_init;
   ops.start = &gtkui_start;
   ops.cleanup = &gtkui_cleanup;
   ops.msg = &gtkui_msg;
   ops.error = &gtkui_error;
   ops.fatal_error = &gtkui_fatal_error;
   ops.input = &gtkui_input;
   ops.progress = &gtkui_progress;
   ops.type = UI_GTK;
   
   ui_register(&ops);
   
}


/*
 * prepare GTK, create the menu/messages window, enter the first loop 
 */
static void gtkui_init(void)
{
   DEBUG_MSG("gtk_init");

   g_thread_init(NULL);
   gdk_threads_init();
   if(!gtk_init_check(0, NULL)) {
   	DEBUG_MSG("GTK+ failed to initialize.");
	   return;
   }

   gtkui_setup();

   /* gui init loop, calling gtk_main_quit will cause
    * this to exit so we can proceed to the main loop
    * later. */
   gdk_threads_enter();
   gtk_main();
   gdk_threads_leave();

   /* remove the keyboard shortcuts for the setup menus */
   gtk_window_remove_accel_group(GTK_WINDOW (window), accel_group);

   GBL_UI->initialized = 1;
}

/*
 * exit ettercap 
 */
static void gtkui_exit(void)
{
   DEBUG_MSG("gtk_exit");

   gtk_main_quit();
   clean_exit(0);
}

/*
 * reset to the previous state
 */
static void gtkui_cleanup(void)
{
   DEBUG_MSG("gtk_cleanup");

   
}

/*
 * print a USER_MSG() extracting it from the queue
 */
static void gtkui_msg(const char *msg)
{
   GtkTextIter iter;

   DEBUG_MSG("gtkui_msg: %s", msg);

   gtk_text_buffer_get_end_iter(msgbuffer, &iter);
   gtk_text_buffer_insert(msgbuffer, &iter, msg, -1);
   gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW (textview), 
                                endmark, 0, FALSE, 0, 0);
   return;
}

/* flush pending messages */
gboolean gtkui_flush_msg(gpointer data)
{
   ui_msg_flush(MSG_ALL);

   return(TRUE);
}

/*
 * print an error
 */
static void gtkui_error(const char *msg)
{
   GtkWidget *dialog;
   
   DEBUG_MSG("gtkui_error: %s", msg);

   dialog = gtk_message_dialog_new(GTK_WINDOW (window), GTK_DIALOG_MODAL, 
                                   GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", msg);
   gtk_window_set_position(GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);

   /* blocking - displays dialog waits for user to click OK */
   gtk_dialog_run(GTK_DIALOG (dialog));

   gtk_widget_destroy(dialog);
   return;
}


/*
 * handle a fatal error and exit
 */
static void gtkui_fatal_error(const char *msg)
{
   /* if the gui is working at this point
      display the message in a dialog */
   if(window)
      gtkui_error(msg);

   /* also dump it to console in case ettercap was started in an xterm */
   fprintf(stderr, "FATAL ERROR: %s\n\n\n", msg);

   clean_exit(-1);
}


/*
 * get an input from the user
 */
void gtkui_input(const char *title, char *input, size_t n, void (*callback)(void))
{
   GtkWidget *dialog, *entry, *label, *hbox, *image;

   dialog = gtk_dialog_new_with_buttons(EC_PROGRAM" Input", GTK_WINDOW (window),
                                        GTK_DIALOG_MODAL, GTK_STOCK_OK, GTK_RESPONSE_OK,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, NULL);
   gtk_dialog_set_has_separator(GTK_DIALOG (dialog), FALSE);
   gtk_container_set_border_width(GTK_CONTAINER (dialog), 5);
   
   hbox = gtk_hbox_new (FALSE, 6);
   gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), hbox, FALSE, FALSE, 0);
   
   image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_QUESTION, GTK_ICON_SIZE_DIALOG);
   gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.0);
   gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);
   
   label = gtk_label_new (title);
   gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
   gtk_label_set_selectable (GTK_LABEL (label), TRUE);
   gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);
   
   entry = gtk_entry_new_with_max_length(n);
   g_object_set_data(G_OBJECT (entry), "dialog", dialog);
   g_signal_connect(G_OBJECT (entry), "activate", G_CALLBACK (gtkui_dialog_enter), NULL);
   
   if (input)
      gtk_entry_set_text(GTK_ENTRY (entry), input); 
   
   gtk_box_pack_start(GTK_BOX (hbox), entry, FALSE, FALSE, 5);
   gtk_widget_show_all (hbox);

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {

      strncpy(input, gtk_entry_get_text(GTK_ENTRY (entry)), n);

      if (callback != NULL) {
         gtk_widget_destroy(dialog);

         callback();
         return;
      }
   }
   gtk_widget_destroy(dialog);
}


/* 
 * show or update the progress bar
 */
static void gtkui_progress(char *title, int value, int max)
{
   static GtkWidget *dialog = NULL;
   static GtkWidget *pbar = NULL;
   
   /* the first time, create the object */
   if (pbar == NULL) {
      dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      gtk_window_set_title(GTK_WINDOW (dialog), EC_PROGRAM);
      gtk_window_set_modal(GTK_WINDOW (dialog), TRUE);
      gtk_window_set_position(GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
      gtk_container_set_border_width(GTK_CONTAINER (dialog), 5);
    
      pbar = gtk_progress_bar_new();
      gtk_container_add(GTK_CONTAINER (dialog), pbar);
      gtk_widget_show(pbar);

      gtk_widget_show(dialog);
   } 
   
   /* the subsequent calls have to only update the object */
   gtk_progress_bar_set_text(GTK_PROGRESS_BAR (pbar), title);
   gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR (pbar), (gdouble)((gdouble)value / (gdouble)max));

   /* a nasty little loop that lets gtk update the progress bar immediately */
   while (gtk_events_pending ())
      gtk_main_iteration ();

   /* 
    * when 100%, destroy it
    */
   if (value == max) {
      gtk_widget_hide(dialog);
      gtk_widget_destroy(pbar);
      gtk_widget_destroy(dialog);
      dialog = NULL;
      pbar = NULL;
   }

}

/*
 * print a message
 */
void gtkui_message(const char *msg)
{
   GtkWidget *dialog;
   
   DEBUG_MSG("gtkui_message: %s", msg);

   dialog = gtk_message_dialog_new(GTK_WINDOW (window), GTK_DIALOG_MODAL, 
                                   GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", msg);
   gtk_window_set_position(GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);

   /* blocking - displays dialog waits for user to click OK */
   gtk_dialog_run(GTK_DIALOG (dialog));

   gtk_widget_destroy(dialog);
   return;
}


/*
 * Create the main interface and enter the second loop
 */

void gtkui_start(void)
{
   guint idle_flush;

   DEBUG_MSG("gtk_start");

   idle_flush = gtk_timeout_add(500, gtkui_flush_msg, NULL);
   
   /* which interface do we have to display ? */
   if (GBL_OPTIONS->read)
      gtkui_sniff_offline();
   else
      gtkui_sniff_live();
   
   /* the main gui loop, once this exits the gui will be destroyed */
   gdk_threads_enter();
   gtk_main();
   gdk_threads_leave();

   gtk_timeout_remove(idle_flush);
}

static void toggle_unoffensive(void)
{
   if (GBL_OPTIONS->unoffensive) {
      GBL_OPTIONS->unoffensive = 0;
   } else {
      GBL_OPTIONS->unoffensive = 1;
   }
}

static void toggle_nopromisc(void)
{
   if (GBL_PCAP->promisc) {
      GBL_PCAP->promisc = 0;
   } else {
      GBL_PCAP->promisc = 1;
   }
}

/*
 * display the initial menu to setup global options
 * at startup.
 */
static void gtkui_setup(void)
{
   GtkTextIter iter;
   GtkWidget *item, *vbox, *scroll, *vpaned, *frame;
   GtkItemFactory *item_factory;
   GClosure *closure = NULL;
   GdkModifierType mods;
   gint keyval;
   char title[50];

   GtkItemFactoryEntry file_menu[] = {
      { "/_File",         "<shift>F",   NULL,             0, "<Branch>" },
      { "/File/_Open",    "<control>O", gtkui_file_open,  0, "<StockItem>", GTK_STOCK_OPEN },
      { "/File/_Save",    "<control>S", gtkui_file_write, 0, "<StockItem>", GTK_STOCK_SAVE },
      { "/File/sep1",     NULL,         NULL,             0, "<Separator>" },
      { "/File/E_xit",    "<control>x", gtkui_exit,       0, "<StockItem>", GTK_STOCK_QUIT },
      { "/_Sniff",        "<shift>S",   NULL,             0, "<Branch>" },
      { "/Sniff/Unified sniffing...",  "<shift>U", gtkui_unified_sniff, 0, "<StockItem>", GTK_STOCK_DND },
      { "/Sniff/Bridged sniffing...",  "<shift>B", gtkui_bridged_sniff, 0, "<StockItem>", GTK_STOCK_DND_MULTIPLE },
      { "/Sniff/sep2",    NULL,         NULL,             0, "<Separator>" },
      { "/Sniff/Set pcap filter...",    "p",       gtkui_pcap_filter,   0, "<StockItem>", GTK_STOCK_PREFERENCES },
      { "/_Options",                    "<shift>O", NULL, 0, "<Branch>" },
      { "/Options/Unoffensive", NULL, toggle_unoffensive, 0, "<ToggleItem>" },
      { "/Options/Promisc mode", NULL, toggle_nopromisc,  0, "<ToggleItem>" }
   };
   gint nmenu_items = sizeof (file_menu) / sizeof (file_menu[0]);

   DEBUG_MSG("gtkui_setup");

   /* create menu window */
   window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
   snprintf(title, 50, "%s %s", EC_PROGRAM, EC_VERSION);
   gtk_window_set_title(GTK_WINDOW (window), title);
   gtk_window_set_default_size(GTK_WINDOW (window), 600, 440);

   g_signal_connect (G_OBJECT (window), "delete_event", G_CALLBACK (gtkui_exit), NULL);

   accel_group = gtk_accel_group_new ();
   item_factory = gtk_item_factory_new (GTK_TYPE_MENU_BAR, "<main>", accel_group);
   gtk_item_factory_create_items (item_factory, nmenu_items, file_menu, NULL);

   /* hidden shortcut to start Unified Sniffing with default interface */
   closure = g_cclosure_new(G_CALLBACK(gtkui_unified_sniff_default), NULL, NULL);
   gtk_accelerator_parse ("u", &keyval, &mods);
   gtk_accel_group_connect(accel_group, keyval, mods, 0, closure);

   vbox = gtk_vbox_new(FALSE, 0);
   gtk_container_add(GTK_CONTAINER (window), vbox);
   gtk_widget_show(vbox);

   main_menu = gtk_item_factory_get_widget (item_factory, "<main>");
   gtk_box_pack_start(GTK_BOX(vbox), main_menu, FALSE, FALSE, 0);
   gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);
   gtk_widget_show(main_menu);

   if(GBL_PCAP->promisc) {
      /* setting the menu item active will toggle this setting */
      /* it will be TRUE after the menu is updated */
      GBL_PCAP->promisc = 0;
      item = gtk_item_factory_get_item(item_factory, "/Options/Promisc mode");
      gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM (item), TRUE);
   }

   if(GBL_OPTIONS->unoffensive) {
      GBL_OPTIONS->unoffensive = 0;
      item = gtk_item_factory_get_item(item_factory, "/Options/Unoffensive");
      gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM (item), TRUE);
   }

   vpaned = gtk_vpaned_new();

   /* notebook for MDI pages */
   frame = gtk_frame_new(NULL);
   gtk_frame_set_shadow_type(GTK_FRAME (frame), GTK_SHADOW_IN);
   gtk_paned_pack1(GTK_PANED(vpaned), frame, TRUE, TRUE);
   gtk_widget_show(frame);

   notebook = gtk_notebook_new();
   gtk_notebook_set_tab_pos(GTK_NOTEBOOK (notebook), GTK_POS_TOP);
   gtk_notebook_set_scrollable(GTK_NOTEBOOK (notebook), TRUE);
   gtk_container_add(GTK_CONTAINER (frame), notebook);
   gtk_widget_show(notebook);

   g_signal_connect(G_OBJECT (notebook), "switch-page", G_CALLBACK(gtkui_page_defocus_tabs), NULL);

   /* messages */
   scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW (scroll),
                                  GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
   gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW (scroll), GTK_SHADOW_IN);
   gtk_paned_pack2(GTK_PANED (vpaned), scroll, FALSE, TRUE);
   gtk_widget_show(scroll);

   textview = gtk_text_view_new();
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW (textview), GTK_WRAP_WORD);
   gtk_text_view_set_editable(GTK_TEXT_VIEW (textview), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW (textview), FALSE);
   gtk_widget_set_size_request(textview, -1, 140);
   gtk_container_add(GTK_CONTAINER (scroll), textview);
   gtk_widget_show(textview);

   msgbuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW (textview));
   gtk_text_buffer_get_end_iter(msgbuffer, &iter);
   endmark = gtk_text_buffer_create_mark(msgbuffer, "end", &iter, FALSE);

   gtk_box_pack_end(GTK_BOX(vbox), vpaned, TRUE, TRUE, 0);
   gtk_widget_show(vpaned);

   gtk_widget_show(window);

   DEBUG_MSG("gtk_setup: end");
}

/*
 * display the file open dialog
 */
static void gtkui_file_open(void)
{
   GtkWidget *dialog;
   char *filename;
   int response = 0;

   DEBUG_MSG("gtk_file_open");

   dialog = gtk_file_selection_new ("Select a pcap file...");

   response = gtk_dialog_run (GTK_DIALOG (dialog));

   if (response == GTK_RESPONSE_OK) {
      gtk_widget_hide(dialog);
      filename = gtk_file_selection_get_filename (GTK_FILE_SELECTION (dialog));
      /* destroy needs to come before read_pcapfile so gtk_main_quit
         can reside inside read_pcapfile, which is why destroy is here
         twice and not after the if() block */
      gtk_widget_destroy (dialog);

      read_pcapfile (filename);
   } else {
      gtk_widget_destroy (dialog);
   }
}

static void read_pcapfile(char *file)
{
   char errbuf[128];
   
   DEBUG_MSG("read_pcapfile %s", file);
   
   SAFE_CALLOC(GBL_OPTIONS->dumpfile, strlen(file)+1, sizeof(char));

   sprintf(GBL_OPTIONS->dumpfile, "%s", file);

   /* check if the file is good */
   if (is_pcap_file(GBL_OPTIONS->dumpfile, errbuf) != ESUCCESS) {
      ui_error("%s", errbuf);
      SAFE_FREE(GBL_OPTIONS->dumpfile);
      return;
   }
   
   /* set the options for reading from file */
   GBL_OPTIONS->silent = 1;
   GBL_OPTIONS->unoffensive = 1;
   GBL_OPTIONS->write = 0;
   GBL_OPTIONS->read = 1;

   gtk_main_quit();
}

/*
 * display the write file menu
 */
static void gtkui_file_write(void)
{
#define FILE_LEN  40
   
   DEBUG_MSG("gtk_file_write");
   
   SAFE_CALLOC(GBL_OPTIONS->dumpfile, FILE_LEN, sizeof(char));

   gtkui_input("Output file :", GBL_OPTIONS->dumpfile, FILE_LEN, write_pcapfile);
}

static void write_pcapfile(void)
{
   FILE *f;
   
   DEBUG_MSG("write_pcapfile");
   
   /* check if the file is writeable */
   f = fopen(GBL_OPTIONS->dumpfile, "w");
   if (f == NULL) {
      ui_error("Cannot write %s", GBL_OPTIONS->dumpfile);
      SAFE_FREE(GBL_OPTIONS->dumpfile);
      return;
   }
 
   /* if ok, delete it */
   fclose(f);
   unlink(GBL_OPTIONS->dumpfile);

   /* set the options for writing to a file */
   GBL_OPTIONS->write = 1;
   GBL_OPTIONS->read = 0;
   
   /* exit the setup interface, and go to the primary one */
   gtk_main_quit();
}

/*
 * display the interface selection dialog
 */
static void gtkui_unified_sniff(void)
{
   char err[PCAP_ERRBUF_SIZE];
   
#define IFACE_LEN  10
   
   DEBUG_MSG("gtk_unified_sniff");
   
   /* if the user has not specified an interface, get the first one */
   if (GBL_OPTIONS->iface == NULL) {
      SAFE_CALLOC(GBL_OPTIONS->iface, IFACE_LEN, sizeof(char));
      strncpy(GBL_OPTIONS->iface, pcap_lookupdev(err), IFACE_LEN - 1);
   }

   /* calling gtk_main_quit will go to the next interface :) */
   gtkui_input("Network interface :", GBL_OPTIONS->iface, IFACE_LEN, gtk_main_quit);
}

/* 
 * start unified sniffing with default interface
 */
static void gtkui_unified_sniff_default(void) {
   char err[PCAP_ERRBUF_SIZE];
   
   #define IFACE_LEN  10

   DEBUG_MSG("gtkui_unified_sniff_default");

   if (GBL_OPTIONS->iface == NULL) { 
      SAFE_CALLOC(GBL_OPTIONS->iface, IFACE_LEN, sizeof(char));
      strncpy(GBL_OPTIONS->iface, pcap_lookupdev(err), IFACE_LEN - 1);
   }

   /* close setup interface and start sniffing */
   gtk_main_quit();
}

/*
 * display the interface selection for bridged sniffing
 */
static void gtkui_bridged_sniff(void)
{
   GtkWidget *dialog, *vbox, *hbox, *image;
   GtkWidget *hbox_big, *label, *entry1, *entry2;
   char err[PCAP_ERRBUF_SIZE];

   DEBUG_MSG("gtk_bridged_sniff");

   dialog = gtk_dialog_new_with_buttons("Bridged Sniffing", GTK_WINDOW (window),
                                        GTK_DIALOG_MODAL, GTK_STOCK_OK, GTK_RESPONSE_OK,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, NULL);
   gtk_container_set_border_width(GTK_CONTAINER (dialog), 5);
   gtk_dialog_set_has_separator(GTK_DIALOG (dialog), FALSE);

   hbox_big = gtk_hbox_new (FALSE, 5);
   gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), hbox_big, FALSE, FALSE, 0);
   gtk_widget_show(hbox_big);

   image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_QUESTION, GTK_ICON_SIZE_DIALOG);
   gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.1);
   gtk_box_pack_start (GTK_BOX (hbox_big), image, FALSE, FALSE, 5);
   gtk_widget_show(image);

   vbox = gtk_vbox_new (FALSE, 2);
   gtk_container_set_border_width(GTK_CONTAINER (vbox), 5);
   gtk_box_pack_start (GTK_BOX (hbox_big), vbox, TRUE, TRUE, 0);
   gtk_widget_show(vbox);

   hbox = gtk_hbox_new(FALSE, 0);
   gtk_box_pack_start(GTK_BOX (vbox), hbox, TRUE, TRUE, 0);
   gtk_widget_show(hbox);

   label = gtk_label_new ("First network interface  : ");
   gtk_misc_set_alignment(GTK_MISC (label), 0, 0.5);
   gtk_box_pack_start(GTK_BOX (hbox), label, TRUE, TRUE, 0);
   gtk_widget_show(label);

   entry1 = gtk_entry_new_with_max_length(IFACE_LEN);
   gtk_entry_set_width_chars (GTK_ENTRY (entry1), 6);
   
   if (GBL_OPTIONS->iface == NULL)
      gtk_entry_set_text (GTK_ENTRY (entry1), pcap_lookupdev(err));
   else
      gtk_entry_set_text (GTK_ENTRY (entry1), GBL_OPTIONS->iface);
   
   gtk_box_pack_start(GTK_BOX (hbox), entry1, FALSE, FALSE, 0);
   gtk_widget_show(entry1);

   hbox = gtk_hbox_new(FALSE, 0);
   gtk_box_pack_start(GTK_BOX (vbox), hbox, TRUE, TRUE, 0);
   gtk_widget_show(hbox);

   label = gtk_label_new ("Second network interface : ");
   gtk_misc_set_alignment(GTK_MISC (label), 0, 0.5);
   gtk_box_pack_start(GTK_BOX (hbox), label, TRUE, TRUE, 0);
   gtk_widget_show(label);

   entry2 = gtk_entry_new_with_max_length(IFACE_LEN);
   gtk_entry_set_width_chars (GTK_ENTRY (entry2), 6);
   gtk_box_pack_start(GTK_BOX (hbox), entry2, FALSE, FALSE, 0);
   gtk_widget_show(entry2);

   if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK) {
      gtk_widget_hide(dialog);

      SAFE_CALLOC(GBL_OPTIONS->iface, IFACE_LEN, sizeof(char));
      SAFE_CALLOC(GBL_OPTIONS->iface_bridge, IFACE_LEN, sizeof(char));

      strncpy(GBL_OPTIONS->iface, gtk_entry_get_text(GTK_ENTRY (entry1)), IFACE_LEN);
      strncpy(GBL_OPTIONS->iface_bridge, gtk_entry_get_text(GTK_ENTRY (entry2)), IFACE_LEN);
      bridged_sniff();
   }

   gtk_widget_destroy(dialog);
}

static void bridged_sniff(void)
{
   set_bridge_sniff();
   
   /* leaves setup menu, goes to main interface */
   gtk_main_quit();
}

/*
 * display the pcap filter dialog
 */
static void gtkui_pcap_filter(void)
{
#define PCAP_FILTER_LEN  50
   
   DEBUG_MSG("gtk_pcap_filter");
   
   SAFE_CALLOC(GBL_PCAP->filter, PCAP_FILTER_LEN, sizeof(char));

   /* 
    * no callback, the filter is set but we have to return to
    * the interface for other user input
    */
   gtkui_input("Pcap filter :", GBL_PCAP->filter, PCAP_FILTER_LEN, NULL);
}

/* used for Profile and Connection details */
GtkTextBuffer *gtkui_details_window(char *title)
{
   GtkWidget *dwindow, *dscrolled, *dtextview;
   GtkWidget *vbox, *hbox, *button;     
                                        
   DEBUG_MSG("gtkui_details_window");
   
   dwindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW (dwindow), title);
   gtk_window_set_default_size(GTK_WINDOW (dwindow), 300, 300);
   gtk_container_set_border_width(GTK_CONTAINER (dwindow), 5);
   gtk_window_set_position(GTK_WINDOW (dwindow), GTK_WIN_POS_CENTER);
   g_signal_connect (G_OBJECT (dwindow), "delete_event", G_CALLBACK (gtk_widget_destroy), NULL);
   
   vbox = gtk_vbox_new(FALSE, 5);
   gtk_container_add(GTK_CONTAINER (dwindow), vbox);
   gtk_widget_show(vbox);
   
   dscrolled = gtk_scrolled_window_new(NULL, NULL); 
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW (dscrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW (dscrolled), GTK_SHADOW_IN);
   gtk_box_pack_start(GTK_BOX(vbox), dscrolled, TRUE, TRUE, 0);
   gtk_widget_show(dscrolled);

   dtextview = gtk_text_view_new();
   gtk_text_view_set_editable(GTK_TEXT_VIEW (dtextview), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW (dtextview), FALSE);
   gtk_container_add(GTK_CONTAINER (dscrolled), dtextview);
   gtk_widget_show(dtextview);

   hbox = gtk_hbox_new(FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
   gtk_widget_show(hbox);

   button = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
   g_signal_connect_swapped(G_OBJECT(button), "clicked", G_CALLBACK(gtk_widget_destroy), dwindow);
   gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
   gtk_widget_show(button);

   gtk_widget_show(dwindow);

   return(gtk_text_view_get_buffer(GTK_TEXT_VIEW (dtextview)));
}

/* append a string to a GtkTextBuffer */
void gtkui_details_print(GtkTextBuffer *textbuf, char *data)
{
   GtkTextIter iter;

   gtk_text_buffer_get_end_iter(textbuf, &iter);
   gtk_text_buffer_insert(textbuf, &iter, data, -1);
}

/* hitting "Enter" key in dialog does same as clicking OK button */
void gtkui_dialog_enter(GtkWidget *widget, gpointer data) {
   GtkWidget *dialog;

   dialog = g_object_get_data(G_OBJECT(widget), "dialog");
   gtk_dialog_response(GTK_DIALOG (dialog), GTK_RESPONSE_OK);
}

/* create a new notebook (tab) page */
GtkWidget *gtkui_page_new(char *title, void (*callback)(void), void (*detacher)(GtkWidget *)) {
   GtkWidget *parent, *label;
   GtkWidget *hbox, *button, *image;

   /* a container to hold the close button and tab label */
   hbox = gtk_hbox_new(FALSE, 0);
   gtk_widget_show(hbox);

   /* the label for the tab title */
   label = gtk_label_new(title);
   gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
   gtk_widget_show(label);

   /* the close button */
   button = gtk_button_new();
   gtk_button_set_relief(GTK_BUTTON (button), GTK_RELIEF_NONE);
   gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
   gtk_widget_set_size_request(button, 20, 20);
   gtk_widget_show(button);

   /* an image for the button */
   image = gtk_image_new_from_stock (GTK_STOCK_CLOSE, GTK_ICON_SIZE_MENU);
   gtk_container_add(GTK_CONTAINER (button), image);
   gtk_widget_show(image);

   /* a parent to pack the contents into */
   parent = gtk_frame_new(NULL);
   gtk_frame_set_shadow_type(GTK_FRAME(parent), GTK_SHADOW_NONE);
   gtk_widget_show(parent);

   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), parent, hbox);

   /* attach callback to destroy the tab/page */
   g_signal_connect(G_OBJECT (button), "clicked", G_CALLBACK(gtkui_page_close), parent);

   /* attach callback to do specific clean-up */
   if(callback)
      g_object_set_data(G_OBJECT (parent), "destroy", callback);

   if(detacher)
      g_object_set_data(G_OBJECT (parent), "detach", detacher);

   gtkui_page_present(parent);

   return(parent);
}

/* show and focus the page containing child */
void gtkui_page_present(GtkWidget *child) {
   int num = 0;

   num = gtk_notebook_page_num(GTK_NOTEBOOK (notebook), child);
   gtk_notebook_set_current_page(GTK_NOTEBOOK (notebook), num);

   gtkui_page_defocus_tabs();
}

/* defocus tab buttons in notebook (gtk bug work-around */
static void gtkui_page_defocus_tabs(void)
{
   /* gtk_notebook_get_n_pages was added in GTK+ 2.2 */
   /* so we'll leave this section out if building on GTK+ 2.0 */
   #if GTK_MINOR_VERSION >= 2
   GList *list = NULL, *curr = NULL;
   GtkWidget *contents, *label;
   int pages = 0;

   /* make sure all the close buttons loose focus */
   for(pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK (notebook)); pages > 0; pages--) {
      contents = gtk_notebook_get_nth_page(GTK_NOTEBOOK (notebook), (pages - 1));
      label = gtk_notebook_get_tab_label(GTK_NOTEBOOK (notebook), contents);

      list = gtk_container_get_children(GTK_CONTAINER (label));
      for(curr = list; curr != NULL; curr = curr->next)
         if(GTK_IS_BUTTON (curr->data))
            gtk_button_leave(GTK_BUTTON (curr->data));
   }
   #endif
}

/* close the page containing the child passed in "data" */
void gtkui_page_close(GtkWidget *widget, gpointer data) {
   GtkWidget *child;
   gint num = 0;
   void (*callback)(void);

   DEBUG_MSG("gtkui_page_close");

   num = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), GTK_WIDGET (data));
   child = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), num);
   g_object_ref(G_OBJECT(child));

   gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), num);

   callback = g_object_get_data(G_OBJECT (child), "destroy");
   if(callback)
      callback();
}

/* close the currently focused notebook page */
void gtkui_page_close_current(void) {
   GtkWidget *child;
   gint num = 0;

   num = gtk_notebook_get_current_page(GTK_NOTEBOOK (notebook));
   child = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), num);

   gtkui_page_close(NULL, child);
}

/* show the context menu when the notebook tabs recieve a mouse right-click */
gboolean gtkui_context_menu(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if(event->button == 3)
        gtk_menu_popup(GTK_MENU(data), NULL, NULL, NULL, NULL, 3, event->time);
    return(FALSE);
}

/* detach the currently focused notebook page into a free window */
void gtkui_page_detach_current(void) {
   void (*detacher)(GtkWidget *);
   GtkWidget *child;
   gint num = 0;

   num = gtk_notebook_get_current_page(GTK_NOTEBOOK (notebook));
   child = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), num);
   g_object_ref(G_OBJECT(child));

   gtk_notebook_remove_page(GTK_NOTEBOOK(notebook), num);
   
   detacher = g_object_get_data(G_OBJECT (child), "detach");
   if(detacher)
      detacher(child);
}

/* change view and focus to the next notebook page */
void gtkui_page_right(void) {
   gtk_notebook_next_page(GTK_NOTEBOOK (notebook));
}

/* change view and focus to previous notebook page */
void gtkui_page_left(void) {
   gtk_notebook_prev_page(GTK_NOTEBOOK (notebook));
}

/* EOF */

// vim:ts=3:expandtab

