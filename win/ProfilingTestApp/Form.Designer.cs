namespace ProfilingTestApp
{
    partial class Form
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.typeField = new System.Windows.Forms.TextBox();
            this.nameField = new System.Windows.Forms.TextBox();
            this.typeLabel = new System.Windows.Forms.Label();
            this.nameLabel = new System.Windows.Forms.Label();
            this.asServerCheckbox = new System.Windows.Forms.CheckBox();
            this.toggleButton = new System.Windows.Forms.Button();
            this.statusBar = new System.Windows.Forms.ProgressBar();
            this.statusLabel = new System.Windows.Forms.Label();
            this.activeHeader = new System.Windows.Forms.Label();
            this.tputHeader = new System.Windows.Forms.Label();
            this.sampleHeader = new System.Windows.Forms.Label();
            this.othersHeader = new System.Windows.Forms.Label();
            this.activeLabel = new System.Windows.Forms.Label();
            this.tputLabel = new System.Windows.Forms.Label();
            this.sampleLabel = new System.Windows.Forms.Label();
            this.othersLabel = new System.Windows.Forms.Label();
            this.promoteButton = new System.Windows.Forms.Button();
            this.SuspendLayout();
            // 
            // typeField
            // 
            this.typeField.Location = new System.Drawing.Point(74, 12);
            this.typeField.Name = "typeField";
            this.typeField.Size = new System.Drawing.Size(100, 20);
            this.typeField.TabIndex = 0;
            this.typeField.Text = "_ymproftest._tcp";
            // 
            // nameField
            // 
            this.nameField.Location = new System.Drawing.Point(74, 38);
            this.nameField.Name = "nameField";
            this.nameField.Size = new System.Drawing.Size(100, 20);
            this.nameField.TabIndex = 1;
            this.nameField.Text = "windows";
            // 
            // typeLabel
            // 
            this.typeLabel.AutoSize = true;
            this.typeLabel.Location = new System.Drawing.Point(33, 15);
            this.typeLabel.Name = "typeLabel";
            this.typeLabel.Size = new System.Drawing.Size(27, 13);
            this.typeLabel.TabIndex = 2;
            this.typeLabel.Text = "type";
            // 
            // nameLabel
            // 
            this.nameLabel.AutoSize = true;
            this.nameLabel.Location = new System.Drawing.Point(33, 41);
            this.nameLabel.Name = "nameLabel";
            this.nameLabel.Size = new System.Drawing.Size(33, 13);
            this.nameLabel.TabIndex = 3;
            this.nameLabel.Text = "name";
            // 
            // asServerCheckbox
            // 
            this.asServerCheckbox.AutoSize = true;
            this.asServerCheckbox.Checked = true;
            this.asServerCheckbox.CheckState = System.Windows.Forms.CheckState.Checked;
            this.asServerCheckbox.Location = new System.Drawing.Point(36, 68);
            this.asServerCheckbox.Name = "asServerCheckbox";
            this.asServerCheckbox.Size = new System.Drawing.Size(69, 17);
            this.asServerCheckbox.TabIndex = 4;
            this.asServerCheckbox.Text = "as server";
            this.asServerCheckbox.UseVisualStyleBackColor = true;
            // 
            // toggleButton
            // 
            this.toggleButton.Location = new System.Drawing.Point(111, 64);
            this.toggleButton.Name = "toggleButton";
            this.toggleButton.Size = new System.Drawing.Size(75, 23);
            this.toggleButton.TabIndex = 5;
            this.toggleButton.Text = "start";
            this.toggleButton.UseVisualStyleBackColor = true;
            // 
            // statusBar
            // 
            this.statusBar.Location = new System.Drawing.Point(54, 126);
            this.statusBar.Name = "statusBar";
            this.statusBar.Size = new System.Drawing.Size(100, 16);
            this.statusBar.TabIndex = 6;
            this.statusBar.Visible = false;
            // 
            // statusLabel
            // 
            this.statusLabel.AutoSize = true;
            this.statusLabel.Location = new System.Drawing.Point(71, 110);
            this.statusLabel.Name = "statusLabel";
            this.statusLabel.Size = new System.Drawing.Size(44, 13);
            this.statusLabel.TabIndex = 7;
            this.statusLabel.Text = "status...";
            this.statusLabel.Visible = false;
            // 
            // activeHeader
            // 
            this.activeHeader.AutoSize = true;
            this.activeHeader.Location = new System.Drawing.Point(13, 162);
            this.activeHeader.Name = "activeHeader";
            this.activeHeader.Size = new System.Drawing.Size(39, 13);
            this.activeHeader.TabIndex = 8;
            this.activeHeader.Text = "active:";
            // 
            // tputHeader
            // 
            this.tputHeader.AutoSize = true;
            this.tputHeader.Location = new System.Drawing.Point(22, 185);
            this.tputHeader.Name = "tputHeader";
            this.tputHeader.Size = new System.Drawing.Size(30, 13);
            this.tputHeader.TabIndex = 9;
            this.tputHeader.Text = "t\'put:";
            // 
            // sampleHeader
            // 
            this.sampleHeader.AutoSize = true;
            this.sampleHeader.Location = new System.Drawing.Point(9, 208);
            this.sampleHeader.Name = "sampleHeader";
            this.sampleHeader.Size = new System.Drawing.Size(43, 13);
            this.sampleHeader.TabIndex = 10;
            this.sampleHeader.Text = "sample:";
            // 
            // othersHeader
            // 
            this.othersHeader.AutoSize = true;
            this.othersHeader.Location = new System.Drawing.Point(13, 231);
            this.othersHeader.Name = "othersHeader";
            this.othersHeader.Size = new System.Drawing.Size(39, 13);
            this.othersHeader.TabIndex = 11;
            this.othersHeader.Text = "others:";
            // 
            // activeLabel
            // 
            this.activeLabel.AutoSize = true;
            this.activeLabel.Location = new System.Drawing.Point(58, 162);
            this.activeLabel.Name = "activeLabel";
            this.activeLabel.Size = new System.Drawing.Size(35, 13);
            this.activeLabel.TabIndex = 12;
            this.activeLabel.Text = "if type";
            // 
            // tputLabel
            // 
            this.tputLabel.AutoSize = true;
            this.tputLabel.Location = new System.Drawing.Point(58, 185);
            this.tputLabel.Name = "tputLabel";
            this.tputLabel.Size = new System.Drawing.Size(49, 13);
            this.tputLabel.TabIndex = 13;
            this.tputLabel.Text = "1.5 mb/s";
            // 
            // sampleLabel
            // 
            this.sampleLabel.AutoSize = true;
            this.sampleLabel.Location = new System.Drawing.Point(58, 208);
            this.sampleLabel.Name = "sampleLabel";
            this.sampleLabel.Size = new System.Drawing.Size(49, 13);
            this.sampleLabel.TabIndex = 14;
            this.sampleLabel.Text = "1.6 mb/s";
            // 
            // othersLabel
            // 
            this.othersLabel.AutoSize = true;
            this.othersLabel.Location = new System.Drawing.Point(58, 231);
            this.othersLabel.Name = "othersLabel";
            this.othersLabel.Size = new System.Drawing.Size(72, 13);
            this.othersLabel.TabIndex = 15;
            this.othersLabel.Text = "some other ifs";
            // 
            // promoteButton
            // 
            this.promoteButton.Enabled = false;
            this.promoteButton.Location = new System.Drawing.Point(61, 265);
            this.promoteButton.Name = "promoteButton";
            this.promoteButton.Size = new System.Drawing.Size(75, 23);
            this.promoteButton.TabIndex = 16;
            this.promoteButton.Text = "promote";
            this.promoteButton.UseVisualStyleBackColor = true;
            // 
            // Form1
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(209, 300);
            this.Controls.Add(this.promoteButton);
            this.Controls.Add(this.othersLabel);
            this.Controls.Add(this.sampleLabel);
            this.Controls.Add(this.tputLabel);
            this.Controls.Add(this.activeLabel);
            this.Controls.Add(this.othersHeader);
            this.Controls.Add(this.sampleHeader);
            this.Controls.Add(this.tputHeader);
            this.Controls.Add(this.activeHeader);
            this.Controls.Add(this.statusLabel);
            this.Controls.Add(this.statusBar);
            this.Controls.Add(this.toggleButton);
            this.Controls.Add(this.asServerCheckbox);
            this.Controls.Add(this.nameLabel);
            this.Controls.Add(this.typeLabel);
            this.Controls.Add(this.nameField);
            this.Controls.Add(this.typeField);
            this.Name = "Form1";
            this.Text = "Form";
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        private System.Windows.Forms.TextBox typeField;
        private System.Windows.Forms.TextBox nameField;
        private System.Windows.Forms.Label typeLabel;
        private System.Windows.Forms.Label nameLabel;
        private System.Windows.Forms.CheckBox asServerCheckbox;
        private System.Windows.Forms.Button toggleButton;
        private System.Windows.Forms.ProgressBar statusBar;
        private System.Windows.Forms.Label statusLabel;
        private System.Windows.Forms.Label activeHeader;
        private System.Windows.Forms.Label tputHeader;
        private System.Windows.Forms.Label sampleHeader;
        private System.Windows.Forms.Label othersHeader;
        private System.Windows.Forms.Label activeLabel;
        private System.Windows.Forms.Label tputLabel;
        private System.Windows.Forms.Label sampleLabel;
        private System.Windows.Forms.Label othersLabel;
        private System.Windows.Forms.Button promoteButton;
    }
}

