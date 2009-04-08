;;; tool-bar.el --- setting up the tool bar
;;
;; Copyright (C) 2000, 2001, 2002, 2003, 2004,
;;   2005, 2006, 2007, 2008 Free Software Foundation, Inc.
;;
;; Author: Dave Love <fx@gnu.org>
;; Keywords: mouse frames

;; This file is part of GNU Emacs.

;; GNU Emacs is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 3, or (at your option)
;; any later version.

;; GNU Emacs is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GNU Emacs; see the file COPYING.  If not, write to the
;; Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
;; Boston, MA 02110-1301, USA.

;;; Commentary:

;; Provides `tool-bar-mode' to control display of the tool-bar and
;; bindings for the global tool bar with convenience functions
;; `tool-bar-add-item' and `tool-bar-add-item-from-menu'.

;; The normal global binding for [tool-bar] (below) uses the value of
;; `tool-bar-map' as the actual keymap to define the tool bar.  Modes
;; may either bind items under the [tool-bar] prefix key of the local
;; map to add to the global bar or may set `tool-bar-map'
;; buffer-locally to override it.  (Some items are removed from the
;; global bar in modes which have `special' as their `mode-class'
;; property.)

;; Todo: Somehow make tool bars easily customizable by the naive?

;;; Code:

;; The autoload cookie doesn't work when preloading.
;; Deleting it means invoking this command won't work
;; when you are on a tty.  I hope that won't cause too much trouble -- rms.
(define-minor-mode tool-bar-mode
  "Toggle use of the tool bar.
With numeric ARG, display the tool bar if and only if ARG is positive.

See `tool-bar-add-item' and `tool-bar-add-item-from-menu' for
conveniently adding tool bar items."
  :init-value nil
  :global t
  :group 'mouse
  :group 'frames
  (and (display-images-p)
       (let ((lines (if tool-bar-mode 1 0)))
	 ;; Alter existing frames...
	 (mapc (lambda (frame)
		 (modify-frame-parameters frame
					  (list (cons 'tool-bar-lines lines))))
	       (frame-list))
	 ;; ...and future ones.
	 (let ((elt (assq 'tool-bar-lines default-frame-alist)))
	   (if elt
	       (setq default-frame-alist (delete elt default-frame-alist)))
	   (add-to-list 'default-frame-alist (cons 'tool-bar-lines lines))))
       (if (and tool-bar-mode
		(display-graphic-p)
		(= 1 (length (default-value 'tool-bar-map)))) ; not yet setup
	   (tool-bar-setup))))

;;;###autoload
;; We want to pretend the toolbar by standard is on, as this will make
;; customize consider disabling the toolbar a customization, and save
;; that.  We could do this for real by setting :init-value above, but
;; that would turn on the toolbar in MS Windows where it is currently
;; useless, and it would overwrite disabling the tool bar from X
;; resources.  If anyone want to implement this in a cleaner way,
;; please do so.
;; -- Per Abrahamsen <abraham@dina.kvl.dk> 2002-02-21.
(put 'tool-bar-mode 'standard-value '(t))

(defvar tool-bar-map (make-sparse-keymap)
  "Keymap for the tool bar.
Define this locally to override the global tool bar.")

(global-set-key [tool-bar]
		'(menu-item "tool bar" ignore
			    :filter (lambda (ignore) tool-bar-map)))

(defun tool-bar-set-file-extension (image-spec-list extension)
  "Set new file extensions for all :file properties
Replace any extensions of :file properties in elements of
IMAGE-SPEC-LIST. An extension may start with a period . or an
underscore. EXTENSION and the original file name extension (starting
with a period) are added to the file name.

E.g. foo_dis.xpm becomes foo_sel.xpm if EXTENSION is '_sel'."
  (mapcar
   (lambda (spec) 
     (let ((f (plist-get spec :file)) 
	    )
        (if (null f)
	    spec
	  ;; need to replace previous extensions, including those
	  ;; starting with _ - 
	  (plist-put spec :file (concat (replace-regexp-in-string "[\.\_].*$" 
								  "" f)
					extension 
					(file-name-extension f t)))
	  )))
   image-spec-list))

(defun tool-bar-get-image-spec (icon)
  (let* ((fg (face-attribute 'tool-bar :foreground))
	 (bg (face-attribute 'tool-bar :background))
	 (colors (nconc (if (eq fg 'unspecified) nil (list :foreground fg))
			(if (eq bg 'unspecified) nil (list :background bg))))
	 (xpm-spec (list :type 'xpm :file (concat icon ".xpm")))
	 (xpm-lo-spec (if (> (display-color-cells) 256)
			  nil
			(list :type 'xpm :file
                              (concat "low-color/" icon ".xpm"))))
	 (png-spec (if (image-type-available-p 'png)
		       (list :type 'png :file (concat icon ".png") 
			     :background "grey")))
	 (pbm-spec (append (list :type 'pbm :file
                                 (concat icon ".pbm")) colors))
	 (xbm-spec (append (list :type 'xbm :file
                                 (concat icon ".xbm")) colors))
	 (image (find-image
		(if (display-color-p)
		    (list png-spec xpm-lo-spec xpm-spec pbm-spec xbm-spec)
		  (list pbm-spec xbm-spec xpm-lo-spec xpm-spec))))
	 (image-sel (find-image
		     (if (display-color-p)
			 (tool-bar-set-file-extension
			  (list png-spec xpm-lo-spec xpm-spec pbm-spec xbm-spec)
			  "_sel")
		       nil)))
	 (image-dis (find-image
		     (if (display-color-p)
			 (tool-bar-set-file-extension
			  (list png-spec xpm-lo-spec xpm-spec pbm-spec xbm-spec)
			  "_dis")
		       nil)))
	 (images (when image ;; image may be nil if not found.
		    (unless (image-mask-p image)
 		     (setq image (append image '(:mask heuristic))))
		   (if (and image-sel image-dis)
		       (progn		     
			 (unless (image-mask-p image-sel)
			   (setq image-sel (append image-sel 
						   '(:mask heuristic))))
			 (unless (image-mask-p image-dis)
			   (setq image-dis (append image-dis 
						   '(:mask heuristic))))
			 (vector image-sel image image-dis image-dis))
		     image)))) 
    (cons image images)))

;;;###autoload
(defun tool-bar-add-item (icon def key &rest props)
  "Add an item to the tool bar.
ICON names the image, DEF is the key definition and KEY is a symbol
for the fake function key in the menu keymap.  Remaining arguments
PROPS are additional items to add to the menu item specification.  See
Info node `(elisp)Tool Bar'.  Items are added from left to right.

ICON is the base name of a file containing the image to use.  The
function will first try to use low-color/ICON.xpm if `display-color-cells'
is less or equal to 256, then ICON.xpm, then ICON.pbm, and finally
ICON.xbm, using `find-image'.

Use this function only to make bindings in the global value of `tool-bar-map'.
To define items in any other map, use `tool-bar-local-item'."
  (apply 'tool-bar-local-item icon def key tool-bar-map props))

;;;###autoload
(defun tool-bar-local-item (icon def key map &rest props)
  "Add an item to the tool bar in map MAP.
ICON names the image, or is structure of the form (IMG . LABEL),
with the image name IMG, and a string with the label of the icon
displayed in the tool-bar as LABEL. LABEL defaults to the symbol
name of KEY.  DEF is the key definition and KEY is a symbol for
the fake function key in the menu keymap Remaining arguments
PROPS are additional items to add to the menu item specification.
See Info node `(elisp)Tool Bar'. Items are added from left to
right.

ICON or IMG is the base name of a file containing the image to
use. The function will first try to use low-color/ICON.xpm if
display-color-cells is less or equal to 256, then ICON.xpm, then
ICON.pbm, and finally ICON.xbm, using `find-image'."
  (let* ((icon-name (if (consp icon) (car icon) icon))
	 (label (if (consp icon) (cdr icon) ""))
	 (is (tool-bar-get-image-spec icon-name))
	 (image (car is))
	 (images (cdr is))) 
    (when (and (display-images-p) image)
      (define-key-after map (vector key)
	`(menu-item ,label 
		    ,def :image ,images ,@props)))))

;;;###autoload
(defun tool-bar-add-item-from-menu (command icon &optional map &rest props)
  "Define tool bar binding for COMMAND in keymap MAP using the given ICON.
This makes a binding for COMMAND in `tool-bar-map', copying its
binding from the menu bar in MAP (which defaults to `global-map'), but
modifies the binding by adding an image specification for ICON.  It
finds ICON just like `tool-bar-add-item'.  PROPS are additional
properties to add to the binding.

MAP must contain appropriate binding for `[menu-bar]' which holds a keymap.

Use this function only to make bindings in the global value of `tool-bar-map'.
To define items in any other map, use `tool-bar-local-item-from-menu'."
  (apply 'tool-bar-local-item-from-menu command icon
	 (default-value 'tool-bar-map) map props))

;;;###autoload
(defun tool-bar-local-item-from-menu (command icon in-map &optional from-map &rest props)
  "Define local tool bar binding for COMMAND using the given ICON.
ICON names the image, or is structure of the form (IMG . LABEL),
with the image name IMG, and a string with the label of the icon
displyed in the tool-bar as LABEL. This function creates a
binding for COMMAND in IN-MAP, copying its binding from the menu
bar in FROM-MAP (which defaults to `global-map'), but modifies
the binding by adding an image specification for ICON. It finds
ICON just like `tool-bar-add-item'. PROPS are additional
properties to add to the binding.

FROM-MAP must contain appropriate binding for `[menu-bar]' which
holds a keymap."
  (unless from-map
    (setq from-map global-map))
  (let* ((icon-name (if (consp icon) (car icon) icon))
	 (label (if (consp icon) (cdr icon)))
	 (menu-bar-map (lookup-key from-map [menu-bar]))
	 (keys (where-is-internal command menu-bar-map))
	 (is (tool-bar-get-image-spec icon-name))
	 (image (car is))
	 (images (cdr is)) 
	 submap key)
    (when (and (display-images-p) image)
      ;; We'll pick up the last valid entry in the list of keys if
      ;; there's more than one.
      (dolist (k keys)
	;; We're looking for a binding of the command in a submap of
	;; the menu bar map, so the key sequence must be two or more
	;; long.
	(if (and (vectorp k)
		 (> (length k) 1))
	    (let ((m (lookup-key menu-bar-map (substring k 0 -1)))
		  ;; Last element in the bound key sequence:
		  (kk (aref k (1- (length k)))))
	      (if (and (keymapp m)
		       (symbolp kk))
		  (setq submap m
			key kk)))))
      (when (and (symbolp submap) (boundp submap))
	(setq submap (eval submap)))
      (let ((defn (assq key (cdr submap))))
	(if (eq (cadr defn) 'menu-item)
	    (define-key-after in-map (vector key)
	      (append `(menu-item ,(or label (car (cddr defn)))) (cdddr defn) 
		      (list :image image) props))
	  (setq defn (cdr defn))
	  (define-key-after in-map (vector key)
	    (let ((rest (cdr defn)))
	      ;; If the rest of the definition starts
	      ;; with a list of menu cache info, get rid of that.
	      (if (and (consp rest) (consp (car rest)))
		  (setq rest (cdr rest)))
	      (append `(menu-item ,(or label (car defn)) ,rest)
		      (list :image image) props))))))))

;;; Set up some global items.  Additions/deletions up for grabs.

(defun tool-bar-setup ()
  ;; People say it's bad to have EXIT on the tool bar, since users
  ;; might inadvertently click that button.
  ;;(tool-bar-add-item-from-menu 'save-buffers-kill-emacs "exit")
  (tool-bar-add-item-from-menu 'find-file '("new" . "New"))
  (tool-bar-add-item-from-menu 'menu-find-file-existing '("open" . "Open"))
  (tool-bar-add-item-from-menu 'dired '("diropen" . "Directory"))
  (tool-bar-add-item-from-menu 'kill-this-buffer "close")
  (tool-bar-add-item-from-menu 'save-buffer '("save" . "Save") nil
			       :visible '(or buffer-file-name
					     (not (eq 'special
						      (get major-mode
							   'mode-class)))))
  (tool-bar-add-item-from-menu 'write-file '("saveas" . "Save As") nil
			       :visible '(or buffer-file-name
					     (not (eq 'special
						      (get major-mode
							   'mode-class)))))
  (tool-bar-add-item-from-menu 'undo "undo" nil
			       :visible '(not (eq 'special (get major-mode
								'mode-class))))
  (tool-bar-add-item-from-menu (lookup-key menu-bar-edit-menu [cut])
			       "cut" nil
			       :visible '(not (eq 'special (get major-mode
								'mode-class))))
  (tool-bar-add-item-from-menu (lookup-key menu-bar-edit-menu [copy])
			       "copy")
  (tool-bar-add-item-from-menu (lookup-key menu-bar-edit-menu [paste])
			       "paste" nil
			       :visible '(not (eq 'special (get major-mode
								'mode-class))))
  (tool-bar-add-item-from-menu 'nonincremental-search-forward "search")
  ;;(tool-bar-add-item-from-menu 'ispell-buffer "spell")

  ;; There's no icon appropriate for News and we need a command rather
  ;; than a lambda for Read Mail.
  ;;(tool-bar-add-item-from-menu 'compose-mail "mail/compose")

  (tool-bar-add-item-from-menu 'print-buffer "print")

  ;; tool-bar-add-item-from-menu itself operates on
  ;; (default-value 'tool-bar-map), but when we don't use that function,
  ;; we must explicitly operate on the default value.

  (let ((tool-bar-map (default-value 'tool-bar-map)))
    (tool-bar-add-item '("preferences" . "Customize") 'customize 'customize
		       :help "Edit preferences (customize)")

    (tool-bar-add-item '("help" . "Help") (lambda ()
				(interactive)
				(popup-menu menu-bar-help-menu))
		       'help
		       :help "Pop up the Help menu"))
  )

(provide 'tool-bar)

;;; arch-tag: 15f30f0a-d0d7-4d50-bbb7-f48fd0c8582f
;;; tool-bar.el ends here
