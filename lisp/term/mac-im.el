;; mac-im.el --- Input Method for Mac OS X -*-coding: iso-2022-7bit;-*-

;; Copyright (C) 2005, 2006, 2007, 2008
;; HASHIMOTO Taiichi <taiichi2@mac.com>
;; Keywords: input method, Mac OS X

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


;;
;; Variables and functions to change a input method 
;;
(defvar mac-input-method-parameters
  '((0 (title . "")
       (cursor-color)
       (cursor-type))
    (1 (title . "あ")
       (cursor-color)
       (cursor-type))
    (2 (title . "繁")
       (cursor-color)
       (cursor-type))
    (3 (title . "가")
       (cursor-color)
       (cursor-type))
    (7 (title . "Cy")
       (cursor-color)
       (cursor-type))
    (25 (title . "簡")
	(cursor-color)
	(cursor-type))
    (29 (title . "EU")
	(cursor-color)
	(cursor-type))
    )
  "Alist of Mac script code vs parameters for input method on MacOSX.")

(defun mac-key-script (lang)
  "Funtion to convert a language name to a key script."
  (cond ((symbolp lang)
	 (cond ((eq lang 'roman) 0)
	       ((eq lang 'japanese) 1)
	       ((eq lang 'traditional-chinese) 2)
	       ((eq lang 'korean) 3)
	       ((eq lang 'cyrillic) 7)
	       ((eq lang 'simple-chinese) 25)
	       ((eq lang 'central-euro-roman) 25)
	       (t -1)))
	((numberp lang) lang)
	(t -1)))

(defun mac-set-input-method-parameter (language key value)
  "Function to set a parameter of a input method."
  (let* ((key-script (mac-key-script language))
	 (lang-param (assq key-script mac-input-method-parameters))
	 (param (assq key lang-param)))
    (if lang-param
	(if param
	    (setcdr param value)
	  (setcdr lang-param (cons (cons key value) (cdr lang-param))))
      (setq mac-input-method-parameters
	    (cons (list key-script (cons key value))
		  mac-input-method-parameters)))))

(defun mac-get-input-method-parameter (language key)
  "Function to get a parameter of a input method."
  (assq key (cdr (assq (mac-key-script language)
		       mac-input-method-parameters))))

(defun mac-input-method-update (lang)
  "Funtion to update parameters of a input method."
  (let ((title (cdr (mac-get-input-method-parameter lang 'title)))
	(ctype (or (cdr (mac-get-input-method-parameter lang 'cursor-type))
		   (cdr (assq 'cursor-type default-frame-alist))
		   default-cursor-type))
	(ccolor (or (cdr (mac-get-input-method-parameter lang 'cursor-color))
		    (cdr (assq 'cursor-color default-frame-alist)))))
    (setq current-input-method-title title)
    (if ctype (setq cursor-type ctype))
    (if ccolor (set-cursor-color ccolor))
    (force-mode-line-update 'all)
    (if isearch-mode (isearch-update))))

(defun mac-toggle-input-method (&optional arg)
  "Function to toggle input method on MacOSX."
  (interactive)
  (let ((lang (mac-get-current-key-script)))
    (if arg
	(progn
	  (make-local-variable 'input-method-function)
	  (make-local-variable 'cursor-type)

	  (setq inactivate-current-input-method-function
		'mac-toggle-input-method)
	  (setq input-method-function nil)
	  (setq describe-current-input-method-function nil)

	  (mac-input-method-update (mac-get-last-key-script))
	  (if (= lang 0) (mac-set-key-script -1)))
      (kill-local-variable 'input-method-function)
      (kill-local-variable 'cursor-type)
      
      (setq describe-current-input-method-function nil)

      (mac-input-method-update 0)
      (if (not (= lang 0)) (mac-set-key-script -17)))))

(defun mac-change-language-to-us ()
  "Function to change language (Apple Key Script) to us."
  (interactive)
  (mac-toggle-input-method nil))

(defun mac-handle-input-method-change (event)
  "Function run when a input method change."
  (interactive "e")
  
  (let ((lang (car (cadr event))))

    (if (and (not current-input-method) (> lang 0))
        (if isearch-mode
            (isearch-toggle-input-method)
          (set-input-method "MacOSX"))
      (if (and current-input-method (= lang 0))
	  (if isearch-mode
	      (isearch-toggle-input-method)
	    (toggle-input-method nil))))

    (mac-input-method-update lang)))


;;
;; Emacs input method for input method on MacOSX.
;;
(register-input-method "MacOSX" "MacOSX" 'mac-toggle-input-method
		       "Mac" "Input Method on MacOSX System")


;;
;; Minor mode of using input methods on MacOS X
;;
(define-minor-mode mac-input-method-mode
  "Use input methods on MacOSX."
  :init-value nil
  :group 'mac
  :global t

  (if mac-input-method-mode
      (progn
	(setq default-input-method "MacOSX")
	(add-hook 'minibuffer-setup-hook 'mac-change-language-to-us))
    (setq default-input-method nil)))

;;
;; Valiable and functions to ignore system shortcut.
;;
(defvar mac-ignore-shortcut nil
  "A list of ignore shortcut on MacOSX.")

(defun mac-add-ignore-shortcut (key)
  (let ((command '(command cmd))
	(option '(option opt))
	(control '(control ctrl ctl)))
    (add-to-list 'mac-ignore-shortcut
		 (cond ((symbolp key)
			(cond ((memq key command) (cons #x100 nil))
			      ((memq key option)  (cons #x800 nil))
			      ((memq key control) (cons #x1000 nil))
			      (t (cons nil nil))))
		       ((numberp key) (cons 0 key))
		       ((listp key) 
			(let ((l key)
			      (k nil)
			      (m 0))
			  (while l
			    (cond ((memq (car l) command)
				   (if (= (logand m #x100) 0)
				       (setq m (logior m #x100))))
				  ((memq (car l) option)
				   (if (= (logand m #x800) 0)
				       (setq m (logior m #x800))))
				  ((memq (car l) control)
				   (if (= (logand m #x1000) 0)
				       (setq m (logior m #x1000))))
				  ((numberp (car l))
				   (if (not k)
				       (setq k (car l)))))
			    (setq l (cdr l)))
			  (cons m k)))
		       (t (cons nil nil))))))


;;
;; Entry Emacs event for inline input method on MacOSX.
;;
(define-key special-event-map
  [mac-change-input-method] 'mac-handle-input-method-change)
      
;;
;; Convert yen to backslash for JIS keyboard.
;;
(defun mac-translate-from-yen-to-backslash () 
  ;; Convert yen to backslash for JIS keyboard.
  (interactive)

  (define-key global-map [2213] nil)
  (define-key global-map [3420] nil)
  (define-key global-map [67111077] nil)
  (define-key global-map [134219941] nil)
  (define-key global-map [201328805] nil)
  (define-key function-key-map [2213] [?\\]) ;; for Intel
  (define-key function-key-map [3420] [?\\]) ;; for PowerPC
  (define-key function-key-map [67111077] [?\C-\\])
  (define-key function-key-map [134219941] [?\M-\\])
  (define-key function-key-map [201328805] [?\C-\M-\\])
)
