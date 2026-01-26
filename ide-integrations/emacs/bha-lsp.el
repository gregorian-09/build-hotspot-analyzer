;;; bha-lsp.el --- Build Hotspot Analyzer LSP client for Emacs -*- lexical-binding: t; -*-

;; Copyright (C) 2026 Build Hotspot Analyzer Contributors

;; Author: Build Hotspot Analyzer Contributors
;; Version: 0.1.0
;; Package-Requires: ((emacs "27.1") (lsp-mode "8.0"))
;; Keywords: languages, c, cpp, lsp, performance
;; URL: https://github.com/yourusername/build-hotspot-analyzer

;;; Commentary:

;; This package provides LSP client integration for the Build Hotspot Analyzer,
;; enabling C/C++ build time optimization suggestions directly in Emacs.

;;; Code:

(require 'lsp-mode)
(require 'json)

(defgroup bha-lsp nil
  "Build Hotspot Analyzer LSP client."
  :group 'lsp-mode
  :prefix "bha-lsp-")

(defcustom bha-lsp-server-path "bha-lsp"
  "Path to the bha-lsp language server executable."
  :type 'string
  :group 'bha-lsp)

(defcustom bha-lsp-auto-analyze nil
  "Automatically analyze project on file open."
  :type 'boolean
  :group 'bha-lsp)

(defvar bha-lsp--suggestions nil
  "Cached build optimization suggestions.")

(defvar bha-lsp--analysis-result nil
  "Cached analysis result.")

(defun bha-lsp--execute-command (command &optional args)
  "Execute LSP command COMMAND with optional ARGS."
  (lsp-request "workspace/executeCommand"
               `(:command ,command
                 :arguments ,(or args []))))

;;;###autoload
(defun bha-lsp-analyze-project (&optional build-dir)
  "Analyze project build performance, optionally using BUILD-DIR."
  (interactive
   (list (read-directory-name "Build directory (empty for auto-detect): "
                              nil nil nil)))
  (let* ((root-dir (lsp-workspace-root))
         (args (list (list :projectRoot root-dir
                          :buildDir (if (and build-dir (not (string-empty-p build-dir)))
                                       build-dir
                                     :json-null)
                          :rebuild :json-false))))
    (message "Analyzing project...")
    (lsp-request-async
     "workspace/executeCommand"
     `(:command "bha.analyze" :arguments ,args)
     (lambda (result)
       (setq bha-lsp--analysis-result result)
       (setq bha-lsp--suggestions (plist-get result :suggestions))
       (let ((num-suggestions (length bha-lsp--suggestions)))
         (message "Analysis complete: %d suggestions found" num-suggestions)
         (when (> num-suggestions 0)
           (bha-lsp-show-suggestions))))
     :error-handler (lambda (error)
                     (message "Analysis failed: %s"
                             (plist-get error :message))))))

;;;###autoload
(defun bha-lsp-show-suggestions ()
  "Display build optimization suggestions in a buffer."
  (interactive)
  (unless bha-lsp--suggestions
    (lsp-request-async
     "workspace/executeCommand"
     '(:command "bha.showMetrics" :arguments [])
     (lambda (result)
       (setq bha-lsp--suggestions (plist-get result :suggestions))
       (bha-lsp--render-suggestions))
     :error-handler (lambda (error)
                     (message "Failed to get suggestions: %s"
                             (plist-get error :message))))
    (return nil))
  (bha-lsp--render-suggestions))

(defun bha-lsp--render-suggestions ()
  "Render suggestions in a dedicated buffer."
  (let ((buf (get-buffer-create "*BHA Suggestions*"))
        (suggestions bha-lsp--suggestions)
        (metrics (plist-get bha-lsp--analysis-result :baselineMetrics)))
    (with-current-buffer buf
      (let ((inhibit-read-only t))
        (erase-buffer)
        (insert (propertize "Build Hotspot Analysis\n" 'face 'bold))
        (insert (make-string 60 ?=) "\n\n")

        (when metrics
          (insert (propertize "Build Metrics:\n" 'face 'bold))
          (insert (format "  Total Build Time: %.2fs\n"
                         (/ (float (plist-get metrics :totalDurationMs)) 1000)))
          (insert (format "  Files Compiled: %d\n\n"
                         (plist-get metrics :filesCompiled))))

        (insert (propertize (format "Optimization Suggestions (%d):\n"
                                   (length suggestions))
                           'face 'bold))
        (insert (make-string 60 ?-) "\n\n")

        (cl-loop for i from 1
                for sug across suggestions
                do
                (let* ((title (plist-get sug :title))
                       (desc (plist-get sug :description))
                       (priority (plist-get sug :priority))
                       (confidence (plist-get sug :confidence))
                       (impact (plist-get sug :estimatedImpact))
                       (id (plist-get sug :id))
                       (priority-str (nth priority '("HIGH" "MEDIUM" "LOW")))
                       (priority-face (nth priority '(error warning success))))

                  (insert (format "[%d] " i))
                  (insert (propertize title 'face 'bold))
                  (insert "\n")

                  (insert (format "    Priority: %s | Confidence: %d%%\n"
                                 (propertize priority-str 'face priority-face)
                                 (floor (* confidence 100))))

                  (insert (format "    %s\n" desc))

                  (when impact
                    (insert (format "    Impact: %.2fs (%.1f%%) • %d files affected\n"
                                   (/ (float (plist-get impact :timeSavedMs)) 1000)
                                   (plist-get impact :percentage)
                                   (plist-get impact :filesAffected))))

                  (insert-button "[Apply]"
                                'action (lambda (_)
                                         (bha-lsp-apply-suggestion id))
                                'follow-link t
                                'help-echo "Apply this suggestion")
                  (insert "\n\n")))

        (insert (make-string 60 ?=) "\n")
        (insert "Press 'q' to close, 'r' to refresh\n"))

      (goto-char (point-min))
      (bha-lsp-suggestions-mode))
    (display-buffer buf)))

;;;###autoload
(defun bha-lsp-apply-suggestion (suggestion-id)
  "Apply suggestion with SUGGESTION-ID."
  (interactive
   (list (if bha-lsp--suggestions
            (let* ((choices (cl-loop for i from 1
                                    for sug across bha-lsp--suggestions
                                    collect (cons (format "[%s] %s - %s"
                                                         (nth (plist-get sug :priority)
                                                             '("HIGH" "MEDIUM" "LOW"))
                                                         (plist-get sug :title)
                                                         (plist-get sug :description))
                                                 (plist-get sug :id))))
                   (choice (completing-read "Apply suggestion: " choices nil t)))
              (cdr (assoc choice choices)))
          (user-error "No suggestions available. Run M-x bha-lsp-analyze-project first"))))

  (when (yes-or-no-p "Apply this suggestion? This will modify your code. ")
    (message "Applying suggestion...")
    (lsp-request-async
     "workspace/executeCommand"
     `(:command "bha.applySuggestion"
       :arguments [(:suggestionId ,suggestion-id)])
     (lambda (result)
       (if (eq (plist-get result :success) t)
           (let ((num-files (length (plist-get result :changedFiles))))
             (message "Suggestion applied successfully. Modified %d files." num-files)
             (revert-buffer t t t))
         (let* ((errors (plist-get result :errors))
                (error-msg (if errors
                              (plist-get (aref errors 0) :message)
                            "Unknown error")))
           (message "Failed to apply suggestion: %s" error-msg))))
     :error-handler (lambda (error)
                     (message "Failed to apply suggestion: %s"
                             (plist-get error :message))))))

(defvar bha-lsp-suggestions-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map (kbd "q") 'quit-window)
    (define-key map (kbd "r") 'bha-lsp-show-suggestions)
    (define-key map (kbd "a") 'bha-lsp-analyze-project)
    map)
  "Keymap for BHA suggestions buffer.")

(define-derived-mode bha-lsp-suggestions-mode special-mode "BHA-Suggestions"
  "Major mode for displaying BHA build optimization suggestions."
  (setq buffer-read-only t))

(lsp-register-client
 (make-lsp-client
  :new-connection (lsp-stdio-connection
                  (lambda () (list bha-lsp-server-path)))
  :major-modes '(c-mode c++-mode objc-mode)
  :priority -1
  :server-id 'bha-lsp
  :activation-fn (lsp-activate-on "c" "cpp" "objective-c")))

;;;###autoload
(defun bha-lsp-setup ()
  "Setup BHA LSP integration."
  (interactive)
  (add-hook 'c-mode-hook #'lsp-deferred)
  (add-hook 'c++-mode-hook #'lsp-deferred)

  (when bha-lsp-auto-analyze
    (add-hook 'lsp-after-open-hook
              (lambda ()
                (when (member major-mode '(c-mode c++-mode))
                  (run-with-timer 2 nil #'bha-lsp-analyze-project))))))

(provide 'bha-lsp)

;;; bha-lsp.el ends here
